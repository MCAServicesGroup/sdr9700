## Summary

Describe what changed and why. Keep this concise; implementation details are useful only when they help reviewers understand a decision or risk.

## Related issue

Link the issue this addresses, when one exists (for example, `Fixes #123`). Small documentation and maintenance changes do not need a separate issue.

## Type of change

- [ ] Bug fix
- [ ] Feature or workflow improvement
- [ ] Radio/protocol behavior change
- [ ] UI/accessibility change
- [ ] Documentation
- [ ] Build/packaging
- [ ] Refactor/cleanup

## Verification

List the commands, automated tests, and manual workflows you used. Include the result and explain anything you could not test.

- Clean build:
- Automated tests:
- Manual checks:

## Radio impact

If this changes IC-9700 LAN, CI-V, scope/waterfall, RX/TX audio, PTT, offset, tone, or memory behavior, describe:

- IC-9700 firmware/version used for testing:
- Band/mode tested:
- Logs, manual references, packet notes, or captures used:
- Known behavior that still needs hardware validation:

Write `None` when the change cannot affect radio behavior.

## Contributor checklist

Check the items that apply. Explain unchecked verification items above; documentation-only changes do not need a build.

- [ ] I read `CONTRIBUTING.md` and followed `CONVENTIONS.md`.
- [ ] I kept the change focused and updated relevant documentation.
- [ ] I ran `make release` and `ctest --test-dir _workspace/build --output-on-failure` for source changes.
- [ ] I ran the pinned formatting and static-analysis checks for C/C++ changes.
- [ ] I added or updated tests where practical.
- [ ] I described any remaining Linux, Apple Silicon macOS, or IC-9700 validation above.
- [ ] I included no credentials, tokens, private station details, or unredacted packet captures.
- [ ] I used `AppSettings` for new client settings and included no unreviewed third-party code in `src/`.
