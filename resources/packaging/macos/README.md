# macOS release packaging

SDR9700's `Release macOS DMG` GitHub Actions workflow builds, tests, audits,
signs, notarizes, staples, and attaches an Apple Silicon DMG whenever a GitHub
Release is published. The release tag must match the CMake project version with
a leading `v`, for example `v26.9.1`.

Configure these GitHub Actions repository secrets before publishing a release:

- `APPLE_CERT_BASE64`: Base64-encoded PKCS#12 (`.p12`) export containing the
  Developer ID Application certificate and its private key.
- `APPLE_CERT_PASSWORD`: Password assigned to the `.p12` export.
- `APPLE_ID`: Apple Account email used for notarization.
- `APPLE_APP_PASSWORD`: App-specific password created for notarization.
- `APPLE_TEAM_ID`: Ten-character Apple Developer Team ID.

Create the certificate secret on macOS without committing the exported
certificate:

```bash
base64 < DeveloperIDApplication.p12 | tr -d '\n' | pbcopy
```

Paste the clipboard contents into the `APPLE_CERT_BASE64` repository secret,
then securely remove the temporary `.p12` if it is no longer needed. Never
store certificates, private keys, or notarization passwords in the repository.

For local packaging, set `SDR9700_SIGN_IDENTITY` to the complete Developer ID
Application identity reported by `security find-identity -v -p codesigning`,
then run:

```bash
make release
make release-dmg
make verify-bundle
```

To notarize the DMG locally, store a `notarytool` Keychain profile, set
`SDR9700_NOTARY_PROFILE` to its name, and run:

```bash
make notarize DMG=src/build/package/SDR9700-<version>-macOS-apple-silicon.dmg
```
