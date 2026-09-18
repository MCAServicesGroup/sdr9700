#include <QtGlobal>
#if !defined(Q_OS_UNIX)
#error "SDR9700 requires a Unix platform for POSIX signal handling."
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSocketNotifier>
#include <QSet>
#include <QThread>
#include <QTimer>
#if defined(Q_OS_MAC) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif
#include <cerrno>
#include <csignal>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

#include "models/RadioModel.h"
#include "gui/MainWindow.h"
#include "AppBuildConfig.h"
#include "AppInfo.h"
#include "CachingQueue.h"
#include "ApplicationLog.h"
#include "LoggingConfiguration.h"
#include "LogCategories.h"
#include "ShutdownTiming.h"
#if defined(Q_OS_MAC)
#include "platform/MacWindowRestoration.h"
#endif

namespace
{
int signalPipe[2] = {-1, -1};
volatile sig_atomic_t shutdownSignalCount = 0;
volatile sig_atomic_t shutdownSignalNumber = 0;
QMutex logOutputMutex;
QMutex logFileIoMutex;
QMutex consoleOutputMutex;
std::unique_ptr<QFile> logFile;
QByteArray logFileBuffer;
bool consoleLogEnabled{false};
bool allConsoleCategoriesEnabled{false};
QSet<QString> consoleLogCategories;
std::atomic_bool logFileAvailable{false};
std::atomic_bool logFileFailureReported{false};
constexpr int kLogFlushIntervalMs = 250;
std::array<char, 16 * 1024> consoleBuffer{};

bool writeBufferedLog(const QByteArray* bufferedData)
{
    if (!bufferedData || bufferedData->isEmpty() || !logFile || !logFile->isOpen())
    {
        return true;
    }

    qsizetype written = 0;
    while (written < bufferedData->size())
    {
        const qint64 result = logFile->write(bufferedData->constData() + written, bufferedData->size() - written);
        if (result <= 0)
        {
            if (!logFileFailureReported.exchange(true))
            {
                std::fprintf(stderr, "Application log file write failed: %s\n",
                             logFile->errorString().toLocal8Bit().constData());
                std::fflush(stderr);
            }
            logFileAvailable.store(false, std::memory_order_release);
            logFile->close();
            return false;
        }
        written += result;
    }
    return true;
}

bool flushBufferedLog(const QByteArray* bufferedData)
{
    if (!writeBufferedLog(bufferedData))
    {
        return false;
    }
    if (logFile && logFile->isOpen() && !logFile->flush())
    {
        if (!logFileFailureReported.exchange(true))
        {
            std::fprintf(stderr, "Application log file flush failed: %s\n",
                         logFile->errorString().toLocal8Bit().constData());
            std::fflush(stderr);
        }
        logFileAvailable.store(false, std::memory_order_release);
        logFile->close();
        return false;
    }
    return true;
}

void requestMacMicrophonePermission(QObject* context)
{
#if defined(Q_OS_MAC) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (!context)
    {
        return;
    }

    QMicrophonePermission permission;
    const Qt::PermissionStatus status = qApp->checkPermission(permission);
    const char* statusText = status == Qt::PermissionStatus::Granted  ? "granted"
                             : status == Qt::PermissionStatus::Denied ? "denied"
                                                                      : "undetermined";
    qInfo(logSystem()).noquote().nospace() << "Microphone permission status=" << statusText;
    if (status == Qt::PermissionStatus::Undetermined)
    {
        qApp->requestPermission(permission, context,
                                [](const QPermission& result)
                                {
                                    qInfo(logSystem()).noquote()
                                        << "Microphone permission"
                                        << (result.status() == Qt::PermissionStatus::Granted ? "granted" : "denied");
                                });
    }
#else
    Q_UNUSED(context)
#endif
}

void flushLogOutput()
{
    if (consoleLogEnabled)
    {
        QMutexLocker consoleLock(&consoleOutputMutex);
        std::fflush(stderr);
    }

    QByteArray bufferedData;
    QMutexLocker fileLock(&logFileIoMutex);
    {
        QMutexLocker bufferLock(&logOutputMutex);
        if (logFileAvailable.load(std::memory_order_acquire))
        {
            logFileBuffer.swap(bufferedData);
        }
        else
        {
            logFileBuffer.clear();
        }
    }
    if (bufferedData.isEmpty())
    {
        return;
    }
    flushBufferedLog(&bufferedData);
}

struct LoggingOptions
{
    bool logEnabled{false};
    bool logFileRequested{false};
    QString logCategories;
    QString logFilePath;
};

void consoleMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString line = ApplicationLog::instance().append(type, context, message);
    const QByteArray encoded = line.toLocal8Bit();

    const QString category =
        context.category ? QString::fromLatin1(context.category).toLower() : QStringLiteral("default");
    const bool consoleCategoryEnabled = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg ||
                                        allConsoleCategoriesEnabled || consoleLogCategories.contains(category);
    const bool fileCategoryEnabled = category != QStringLiteral("ci-v") || type == QtWarningMsg ||
                                     type == QtCriticalMsg || type == QtFatalMsg || allConsoleCategoriesEnabled ||
                                     consoleLogCategories.contains(category);
    if (consoleLogEnabled && consoleCategoryEnabled)
    {
        QMutexLocker consoleLock(&consoleOutputMutex);
        std::fprintf(stderr, "%s\n", encoded.constData());
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        {
            std::fflush(stderr);
        }
    }
    if (logFileAvailable.load(std::memory_order_acquire) && fileCategoryEnabled)
    {
        {
            QMutexLocker bufferLock(&logOutputMutex);
            logFileBuffer.append(encoded);
            logFileBuffer.append('\n');
        }
        // Critical records are durability boundaries. The buffer swap is
        // constant-time; potentially slow filesystem work happens after the
        // logging mutex has been released.
        if (type == QtCriticalMsg || type == QtFatalMsg)
        {
            flushLogOutput();
        }
    }

    if (type == QtFatalMsg)
    {
        std::abort();
    }
}

QString quietLoggingRules()
{
    return QStringLiteral("*.debug=false\n"
                          "*.info=false\n"
                          "system.info=true\n"
                          "gui.info=true\n"
                          "radio.info=true\n"
                          "udp.info=true\n"
                          "audio.info=true\n"
                          "audioconverter.info=true\n"
                          "icom-rc-28.info=true\n"
                          "*.warning=true\n"
                          "*.critical=true");
}

QStringList parseLogCategories(const QString& value)
{
    QStringList categories;
    for (QString category : value.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        category = category.trimmed().toLower();
        if (!category.isEmpty())
        {
            categories.append(category);
        }
    }
    return categories;
}

QString qtLoggingCategoryName(const QString& requestedCategory)
{
    // Command-line category matching is intentionally case-insensitive, but
    // Qt logging filter rules match the declared category name verbatim.
    if (requestedCategory == QStringLiteral("spectrumscope"))
    {
        return QStringLiteral("spectrumScope");
    }
    return requestedCategory;
}

QString loggingRulesForOptions(const LoggingOptions& options)
{
    if (!options.logEnabled)
    {
#if SDR9700_DEBUG_BUILD
        return QStringLiteral("*.debug=true\n"
                              "*.info=true\n"
                              "*.warning=true\n"
                              "*.critical=true");
#else
        return quietLoggingRules();
#endif
    }

    const QStringList requested = parseLogCategories(options.logCategories);
    if (requested.contains(QStringLiteral("all")))
    {
        return QStringLiteral("*.debug=true\n"
                              "*.info=true\n"
                              "*.warning=true\n"
                              "*.critical=true");
    }

    QStringList rules{quietLoggingRules()};
    for (const QString& category : requested)
    {
        const QString qtCategory = qtLoggingCategoryName(category);
        rules.append(QStringLiteral("%1.debug=true").arg(qtCategory));
        rules.append(QStringLiteral("%1.info=true").arg(qtCategory));
    }
    return rules.join(QLatin1Char('\n'));
}

bool consumeOptionValue(const QStringList& arguments, int* index, QString* value)
{
    if (*index + 1 >= arguments.size())
    {
        return false;
    }
    *value = arguments.at(*index + 1);
    ++(*index);
    return true;
}

bool extractLoggingOptions(const QStringList& arguments, QStringList* parserArguments, LoggingOptions* options,
                           QString* error)
{
    parserArguments->clear();
    parserArguments->append(arguments.value(0));

    for (int index = 1; index < arguments.size(); ++index)
    {
        const QString arg = arguments.at(index);
        if (arg == QStringLiteral("--log"))
        {
            *error = QStringLiteral("--log requires categories; use --log=all or --log=<categories>");
            return false;
        }
        if (arg.startsWith(QStringLiteral("--log=")))
        {
            options->logEnabled = true;
            options->logCategories = arg.mid(QStringLiteral("--log=").size());
            if (options->logCategories.trimmed().isEmpty())
            {
                *error = QStringLiteral("--log requires categories; use --log=all or --log=<categories>");
                return false;
            }
            continue;
        }
        if (arg == QStringLiteral("--log-file"))
        {
            options->logFileRequested = true;
            if (!consumeOptionValue(arguments, &index, &options->logFilePath))
            {
                *error = QStringLiteral("--log-file requires a path");
                return false;
            }
            continue;
        }
        if (arg.startsWith(QStringLiteral("--log-file=")))
        {
            options->logFileRequested = true;
            options->logFilePath = arg.mid(QStringLiteral("--log-file=").size());
            continue;
        }
        parserArguments->append(arg);
    }
    return true;
}

bool openLogFile(const QString& path)
{
    if (path.isEmpty())
    {
        std::fprintf(stderr, "--log-file requires a non-empty path\n");
        return false;
    }

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        std::fprintf(stderr, "Could not open log file: %s\n", path.toLocal8Bit().constData());
        return false;
    }
    logFile = std::move(file);
    logFileAvailable.store(true, std::memory_order_release);
    return true;
}

void handleForcedUnixExit(int signalNumber)
{
    _exit(128 + signalNumber);
}

void handleUnixSignal(int signalNumber)
{
    const int savedErrno = errno;
    shutdownSignalNumber = signalNumber;
    if (shutdownSignalCount > 0)
    {
        _exit(128 + signalNumber);
    }
    shutdownSignalCount = 1;

    const char byte = static_cast<char>(signalNumber);
    if (signalPipe[1] != -1)
    {
        write(signalPipe[1], &byte, sizeof(byte));
    }
    alarm(sdr9700::shutdownTiming::kSignalForcedExitSeconds);
    errno = savedErrno;
}

void installUnixSignalHandlers()
{
    if (pipe(signalPipe) != 0)
    {
        return;
    }
    for (int fd : signalPipe)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1)
        {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handleUnixSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    struct sigaction alarmAction;
    memset(&alarmAction, 0, sizeof(alarmAction));
    alarmAction.sa_handler = handleForcedUnixExit;
    sigemptyset(&alarmAction.sa_mask);
    sigaction(SIGALRM, &alarmAction, nullptr);
}

void configureQtMultimediaEnvironment()
{
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    {
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "");
    }
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"))
    {
        qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", "");
    }
}
} // namespace

int main(int argc, char* argv[])
{
    std::setvbuf(stderr, consoleBuffer.data(), _IOLBF, consoleBuffer.size());
    configureQtMultimediaEnvironment();

    // Native file/color dialogs live outside Qt's QObject tree and therefore
    // cannot participate in the typed automation inventory. Only an explicitly
    // automation-enabled process substitutes Qt dialogs, preserving the normal
    // native desktop experience for every ordinary launch while making every
    // modal control discoverable during automation sessions.
    for (int index = 1; index < argc; ++index)
    {
        if (QString::fromLocal8Bit(argv[index]) == QLatin1String("--enable-automation"))
        {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
            break;
        }
    }

    LoggingConfiguration::applyBaseRules(quietLoggingRules());
    qInstallMessageHandler(consoleMessageHandler);
    installUnixSignalHandlers();

    QApplication app(argc, argv);
#if defined(Q_OS_MAC)
    configureMacWindowRestoration(app);
#endif
    app.setApplicationName("SDR9700");
    app.setApplicationDisplayName(QString::fromLatin1(APP_DISPLAY_NAME));
    app.setOrganizationName("SDR9700");
    app.setApplicationVersion(APP_VERSION);
#if defined(Q_OS_LINUX)
    app.setDesktopFileName(QStringLiteral("sdr9700"));
#endif
    app.setWindowIcon(QIcon(QStringLiteral(":/images/icons/sdr9700_app_icon.png")));

    LoggingOptions loggingOptions;
    QStringList parserArguments;
    QString loggingOptionError;
    if (!extractLoggingOptions(QCoreApplication::arguments(), &parserArguments, &loggingOptions, &loggingOptionError))
    {
        std::fprintf(stderr, "%s\n", loggingOptionError.toLocal8Bit().constData());
        return 1;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Icom IC-9700 Client");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringLiteral("log"),
                                        QStringLiteral("Enable console logging; use --log=all or --log=<categories>."),
                                        QStringLiteral("categories")));
    parser.addOption(QCommandLineOption(QStringLiteral("log-file"),
                                        QStringLiteral("Append formatted console logs to <path>."),
                                        QStringLiteral("path")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("enable-automation"),
        QStringLiteral("Enable the local receive-control automation bridge (transmit controls remain unavailable).")));
    parser.process(parserArguments);

    if (loggingOptions.logFileRequested && !openLogFile(loggingOptions.logFilePath))
    {
        return 1;
    }
    consoleLogEnabled = loggingOptions.logEnabled;
    const QStringList requestedCategories = parseLogCategories(loggingOptions.logCategories);
    allConsoleCategoriesEnabled = requestedCategories.contains(QStringLiteral("all"));
    consoleLogCategories = QSet<QString>(requestedCategories.begin(), requestedCategories.end());
    LoggingConfiguration::applyBaseRules(loggingRulesForOptions(loggingOptions));

    QThread logFlushThread;
    QTimer logFlushTimer;
    logFlushTimer.setInterval(kLogFlushIntervalMs);

    app.setStyle("Fusion");
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(30, 30, 30));
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Base, QColor(20, 20, 20));
    dark.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::Button, QColor(50, 50, 50));
    dark.setColor(QPalette::ButtonText, Qt::white);
    dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    dark.setColor(QPalette::Link, QColor(56, 216, 255));
    dark.setColor(QPalette::LinkVisited, QColor(153, 204, 255));
    app.setPalette(dark);
    app.setStyleSheet(QStringLiteral("QComboBox { padding-left: 10px; padding-right: 24px; }"
                                     "QComboBox QAbstractItemView::item { padding: 4px 10px; }"));

    auto model = std::make_unique<RadioModel>();
    auto window = std::make_unique<MainWindow>(model.get());
    if (parser.isSet(QStringLiteral("enable-automation")) && !window->startAutomationBridge())
    {
        std::fprintf(stderr, "Unable to start the local automation bridge.\n");
        return 1;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [&window, &model]()
                     {
                         // Destroy widgets while QApplication, its style, and the
                         // event dispatcher are still alive. The window keeps
                         // pointers to the model, so the UI must go first.
                         window.reset();
                         model.reset();
                         CachingQueue::shutdownInstance();
                     });

    if (signalPipe[0] != -1)
    {
        auto* signalNotifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, window.get());
        // Route Unix signals through Qt so closeEvent() fires and the radio
        // disconnect path gets one bounded chance to clean up. The POSIX signal
        // handler also arms SIGALRM and exits immediately on a second signal;
        // that protects process managers and test runs when the GUI thread is
        // wedged before this notifier can run.
        QObject::connect(
            signalNotifier, &QSocketNotifier::activated, &app,
            [&app, signalNotifier, &window](int fd)
            {
                signalNotifier->setEnabled(false);
                char bytes[16];
                while (read(fd, bytes, sizeof(bytes)) > 0)
                {
                }
                if (window)
                {
                    window->close();
                }
                QApplication::closeAllWindows();
                QTimer::singleShot(
                    sdr9700::shutdownTiming::kSignalForcedExitSeconds * 1000, &app,
                    []() { QCoreApplication::exit(128 + int(shutdownSignalNumber ? shutdownSignalNumber : SIGTERM)); });
            });
    }

    const bool periodicLogFlushRequired = consoleLogEnabled || (logFile && logFile->isOpen());
    const auto stopLogFlushThread = [&]()
    {
        if (!logFlushThread.isRunning())
        {
            flushLogOutput();
            return;
        }

        QThread* const applicationThread = app.thread();
        const bool stopped = QMetaObject::invokeMethod(
            &logFlushTimer,
            [&logFlushTimer, applicationThread]()
            {
                logFlushTimer.stop();
                flushLogOutput();
                logFlushTimer.moveToThread(applicationThread);
            },
            Qt::BlockingQueuedConnection);
        logFlushThread.quit();
        logFlushThread.wait();
        if (!stopped)
        {
            flushLogOutput();
        }
    };
    if (periodicLogFlushRequired)
    {
        logFlushTimer.moveToThread(&logFlushThread);
        QObject::connect(&logFlushThread, &QThread::started, &logFlushTimer, qOverload<>(&QTimer::start));
        QObject::connect(&logFlushTimer, &QTimer::timeout, &logFlushTimer, &flushLogOutput);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, stopLogFlushThread);
        logFlushThread.start();
    }
    window->show();
    QTimer::singleShot(0, window.get(), [context = window.get()]() { requestMacMicrophonePermission(context); });

    const int exitCode = app.exec();

    // aboutToQuit performs the normal ordered teardown. These calls also cover
    // an early event-loop exit that bypasses that signal.
    window.reset();
    model.reset();
    CachingQueue::shutdownInstance();
    stopLogFlushThread();
    flushLogOutput();

    // Qt can emit messages while QApplication and its plugin loaders are
    // destroyed. Stop routing those late messages through application-owned
    // logging state, whose static destruction order relative to Qt is not
    // defined.
    qInstallMessageHandler(nullptr);

    return exitCode;
}
