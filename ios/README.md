# CamBridge iOS Stage 1

This target is a native Swift/SwiftUI + AVFoundation camera capture app. It
does not yet encode, send, or discover a Windows PC.

## Windows-to-iPhone verification path

Windows is the source-authoring and static-validation environment. Apple does
not provide an Xcode/device-signing workflow that runs entirely on Windows.
Use one of these reproducible paths for a real iPhone 17 check:

### Selected: GitHub Actions unsigned IPA and Windows Sideloadly

The repository workflow `.github/workflows/ios-device-ipa.yml` runs on the
GitHub Actions `xcode-27` macOS image. It asserts the iOS 27 SDK, runs the
tests, builds for a generic iOS device with signing disabled, and packages
`CamBridge.app` into `CamBridge-unsigned.ipa`.

Actions does not receive an Apple ID, certificate, provisioning profile, or
signing secret. The Artifact is intentionally unsigned.

Download the Artifact from the completed Actions run, unzip it on Windows, and
use the existing Windows Sideloadly workflow:

1. Keep AltStore PAL installed; this workflow does not remove or modify PAL.
2. Connect the iPhone 17 running iOS 27 to Windows and trust the computer if
   prompted.
3. Open Sideloadly, choose the iPhone, and drag `CamBridge-unsigned.ipa` into
   the IPA field.
4. Sign with the Apple ID configured in Sideloadly and start the installation.
5. Launch CamBridge and grant camera permission.

The app bundle identifier is `com.kukeke.cambridge.dev.20260827`, kept
separate from the existing AltStore/PAL apps and future CamBridge production
identifiers.

The equivalent commands executed by Actions are:

```sh
xcodebuild test \
  -project ios/CamBridge/CamBridge.xcodeproj \
  -scheme CamBridge \
  -destination 'platform=iOS Simulator,name=iPhone 17,OS=27.0' \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO

xcodebuild -project ios/CamBridge/CamBridge.xcodeproj \
  -scheme CamBridge -configuration Release \
  -destination 'generic/platform=iOS' archive \
  -archivePath build/CamBridge.xcarchive

APP_PATH=build/CamBridge.xcarchive/Products/Applications/CamBridge.app
mkdir -p build/Payload
ditto "$APP_PATH" build/Payload/CamBridge.app
ditto -c -k --sequesterRsrc --keepParent build/Payload build/CamBridge-unsigned.ipa
```

The workflow verifies that the archive has no `_CodeSignature` directory
before uploading the IPA Artifact. Sideloadly is the only signing step in this
selected path.

### Optional Apple-signed paths

TestFlight or a registered-device development build remains possible from a
Mac, but is not needed for the selected Windows Sideloadly path. Apple
development provisioning and App Store Connect distribution are separate
signing flows.

### Registered-device development build

Register the iPhone UDID in the Apple Developer account, use Xcode automatic
signing on macOS, connect the device to Xcode, and run the `CamBridge` scheme
on that device. An ad hoc export is also possible for registered devices.
Development and ad hoc profiles are not interchangeable with TestFlight
distribution profiles.

### Unspecified Windows sideloading

Windows may prepare sources and artifacts, but unofficial IPA sideload tools
are not the acceptance path. Their provisioning validity, signing identity,
device compatibility, and repeatability vary. A Windows static pass is never
reported as an iPhone 17 physical-device pass.

## Stage 1 acceptance checklist

- Confirm camera permission is granted.
- Select back and front cameras and inspect the actual lens list.
- Inspect every listed format's dimensions, FPS range, pixel format, HDR, and
  binned status.
- Select 1920x1080 / 60 FPS and confirm the active format readback.
- Confirm Actual FPS stays around 59-60 for at least 10 minutes.
- Record dropped frames and their reasons.
- Select each lens that claims 1080p60 and repeat the measurement.
- Change orientation and confirm output dimensions and transform metadata.
- Confirm unsupported choices remain unsupported and do not become 30 FPS.
