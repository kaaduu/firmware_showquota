# Firmware Quota Android Widget

An Android widget and app that replicates the firmware quota progress bar from the MATE panel applet.

## Features

- **Progress Bar**: Color-coded fill showing quota usage percentage
  - Green (< 50%): RGB(51, 199, 77)
  - Yellow (50-80%): RGB(242, 191, 51) with dark text
  - Red (≥ 80%): RGB(232, 71, 97)
- **Percentage Overlay**: Bold text showing current percentage
- **Window Timer Line**: Optional 2px countdown bar at bottom showing remaining time in 5-hour window
  - White when main bar is green
  - Cyan when main bar is yellow or red
- **Delta History**: Overlay showing last 5 usage changes as colored segments
- **Stale Indicator**: Orange border and diagonal hatch pattern when data is stale
- **Auto-refresh**: Updates every 5 minutes (app, widget, notification)
- **App Details View**: When opened, shows window + weekly quota details (panel-style)
  - Window reset countdown (when server provides `windowReset`)
  - Weekly usage + weekly reset countdown (when available)
  - Manual resets remaining (`windowResetsRemaining`)
  - Diagnostics collapsed behind a "More details" toggle

## Project Structure

```
android-app/
├── app/
│   └── src/
│       └── main/
│           ├── AndroidManifest.xml
│           ├── java/com/firmware/quota/
│           │   ├── MainActivity.java         # Main app activity
│           │   ├── QuotaProgressBar.java     # Custom progress bar view
│           │   ├── QuotaWidgetProvider.java  # Home screen widget
│           │   ├── QuotaApiClient.java       # API client
│           │   ├── QuotaData.java            # Data model
│           │   └── QuotaPreferences.java     # Encrypted preferences
│           └── res/
│               ├── layout/
│               │   ├── activity_main.xml     # Main activity layout
│               │   ├── widget_layout.xml     # Widget layout
│               │   └── notification_quota.xml # Notification layout
│               ├── values/
│               │   ├── colors.xml            # Color definitions
│               │   ├── strings.xml           # String resources
│               │   └── styles.xml            # App theme
│               └── xml/
│                   └── widget_info.xml       # Widget configuration
├── build.gradle                              # Root build file
├── settings.gradle                           # Project settings
└── gradle.properties                         # Gradle properties

```

## Building

1. Open the project in Android Studio
2. Sync Gradle files
3. Build → Build Bundle(s) / APK(s) → Build APK(s)

CLI build:

```bash
./gradlew -p android-app :app:assembleDebug
```

## Usage

### Main App
- Launch the app to view the quota progress bar and details
- Use menu to set/clear API key
- Auto-refreshes every 5 minutes

### Home Screen Widget
- Long-press on home screen
- Select "Widgets"
- Find "Firmware Quota Widget"
- Drag to home screen

## API Key

The app requires a Firmware API key to fetch quota data:

1. Get your API key from https://app.firmware.ai
2. Open the app menu → "Set API Key"
3. Enter your key (format: `fw_api_...` or token)
4. The key is encrypted and stored securely using Android Keystore

## API Endpoint

- URL: `https://app.firmware.ai/api/v1/quota`
- Auth: Bearer token (supports multiple auth methods)
- Response (current):
  - `windowUsed` (fraction 0..1)
  - `windowReset` (UTC ISO8601)
  - `weeklyUsed` (fraction 0..1)
  - `weeklyReset` (UTC ISO8601)
  - `windowResetsRemaining` (int)

Legacy fields may still appear (`used`, `reset`) and are supported for backward compatibility.

## Permissions

- `INTERNET`: Required to fetch quota data from the API
- `POST_NOTIFICATIONS` (Android 13+): Required only if you use the optional foreground notification

## Design

The widget replicates the visual design of the original MATE panel applet:
- Semi-transparent black background (alpha 0.55)
- White border (orange when stale)
- Bold percentage text with shadow
- Color-coded fill based on usage level
- Window timer countdown at bottom edge
- Delta history overlay at leading edge of fill

## Compatibility

- Android API 21+ (Android 5.0 Lollipop)
- AndroidX libraries
- Material Design components
