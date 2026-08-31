export const TEXT = Object.freeze({
  noVideoInputs: "利用可能なカメラがありません",
  unknownCamera: "カメラ（名称未取得）",
  enumerateUnavailable: "カメラ一覧API（navigator.mediaDevices.enumerateDevices）が利用できません",
  matrixSuccess: "成功",
  matrixErrorResult: "エラー",
  noValue: "—",
  missingFramesLabel: "要求FPS基準の不足フレーム数",
  deviceMismatch: "デバイス不一致（要求したカメラと実際のカメラが一致しません）",
  probeSuccess: "60fps成立",
  probeTrackSuccessMeasuredLow: "Trackは60fpsですが実測が低い",
  probeActualBelow: "実際のTrackは60fps未満",
  probeError: "エラー",
  cameraLabelUnavailable: "カメラ {number}（名称未取得）",
  cameraSelectionPending: "カメラ情報は権限取得後に表示されます",
  captureIdle: "● 待機中",
  captureStarting: "● 必須条件で開始しています…",
  cameraSelectionPriming: "● 背面カメラの識別情報を取得しています…",
  captureRunning: "● 動作中",
  captureStopped: "● 停止中",
  measurementUnavailable: "測定不可（requestVideoFrameCallbackが利用できません）",
  fpsUnit: "fps",
  tenSecondLabel: "10秒平均",
  codecProbeUnavailable: "対応コーデックAPIを利用できません",
  noCodecs: "Safariからvideo codecが返されませんでした",
  matrixIdle: "● 待機中",
  matrixNoInputs: "● 利用可能なカメラがありません",
  matrixObserving: "● {camera}: {width}×{height} / {fps}fps — 10秒間観察中",
  matrixComplete: "● 診断完了：{count}件",
  matrixError: "● 診断エラー：{message}",
  constraintNoInputs: "● 利用可能なカメラがありません",
  constraintObserving: "● {camera}: {probe} — 10秒間観察中",
  constraintComplete: "● 60fps条件診断完了：{count}件",
  constraintError: "● 60fps条件診断エラー：{message}",
  stabilityIdle: "● 待機中",
  stabilityStarting: "● 1080p60安定性テストを開始しています…",
  stabilityRunning: "● 1080p60安定性テスト実行中",
  stabilityStopped: "● 安定性テストは停止しました",
  stabilityNoCamera: "● 背面カメラを選択してから安定性テストを開始してください",
  stabilityComplete: "● 1080p60安定性テスト完了：{status}",
  stabilityPassed: "PASS",
  stabilityFailed: "FAIL",
  stabilityCopied: "● 安定性テストJSONをクリップボードへコピーしました",
  webrtcIdle: "● WebRTC未接続",
  webrtcConnecting: "● WebRTC接続中…",
  webrtcOffered: "● Offer送信済み：WindowsのAnswerを待っています",
  webrtcConnected: "● WebRTC接続済み",
  webrtcClosed: "● WebRTC切断",
  webrtcFailed: "● WebRTC接続失敗：{message}",
  webrtcRequiresExactTrack: "1920×1080 / 60fpsの実カメラTrackが必要です",
  webrtcAlreadyActive: "WebRTC senderはすでに接続処理中です",
  webrtcRequiresSession: "WebRTC signaling session IDが必要です",
  webrtcRequiresHttps: "WebRTC signalingにはHTTPSが必要です",
  webrtcH264Unavailable: "SafariからH.264 capabilityが公開されていません",
  webrtcCodecPreferenceUnavailable: "setCodecPreferencesが利用できないためH.264を固定できません",
  webrtcSocketClosed: "signaling WebSocketが開いていません",
  webrtcSessionMismatch: "signaling sessionが一致しません",
  webrtcSocketError: "signaling WebSocketでエラーが発生しました",
  webrtcUnexpectedMessage: "予期しないsignaling messageです",
  webrtcSignalingError: "signalingでエラーが通知されました：{code}",
  clipboardCopied: "● {format}をクリップボードへコピーしました",
  clipboardFailed: "クリップボードへのコピーに失敗しました：{message}",
  mismatch: "{capture}は利用できません：実際のカメラ設定が要求値と一致しません",
  captureUnavailable: "{capture}は利用できません：{error}",
  captureNoTrack: "カメラ映像トラックを取得できませんでした",
  playError: "プレビューを開始できませんでした",
  errorFallback: "カメラ処理中にエラーが発生しました",
  statuses: Object.freeze({
    match: "成功",
    A: "非対応（A：対応範囲の最大FPSが60未満）",
    B: "不一致（B：対応範囲は60以上ですが、実際のFPSが60未満）",
    C: "不一致（C：Track FPSは60ですが、実測FPSが低い）",
    "other-mismatch": "不一致",
    "device-mismatch": "デバイス不一致（要求したカメラと実際のカメラが一致しません）",
    "getUserMedia-failed": "エラー",
    "device-id-missing": "エラー（deviceIdを取得できないため試行しません）",
    "no-video-track": "エラー",
    "observation-failed": "エラー",
  }),
});

const ERROR_DESCRIPTIONS = Object.freeze({
  OverconstrainedError: "指定したカメラ設定を満たせません",
  NotAllowedError: "カメラの使用が許可されていません",
  NotFoundError: "利用可能なカメラが見つかりません",
  NotReadableError: "カメラを開始できません。他のアプリが使用している可能性があります",
  DeviceIdentityUnavailable: "Safariは撮影中でもカメラのdeviceIdを公開しませんでした。個別カメラを特定できないため診断を実行しません",
  AbortError: "カメラの開始が中断されました",
  SecurityError: "カメラへのアクセスがセキュリティ設定で拒否されました",
  TypeError: "カメラ要求の形式が正しくありません",
});

function interpolate(template, values) {
  return template.replace(/\{(\w+)\}/g, (_, key) => String(values[key] ?? ""));
}

export function formatRequestedCapture(settings) {
  return `${settings.resolution.width}×${settings.resolution.height} / ${settings.frameRate}fps`;
}

export function formatActualCapture(settings = {}) {
  return `${settings.width ?? "?"}×${settings.height ?? "?"} / ${settings.frameRate ?? "?"}fps`;
}

export function formatOutputPlan(plan) {
  return `${plan.outputDimensions.width}×${plan.outputDimensions.height} / 回転 ${plan.rotationDegrees}° / transform: ${plan.transportTransform}`;
}

export function formatCameraLabel(label, index) {
  return label || TEXT.cameraLabelUnavailable.replace("{number}", String(index + 1));
}

export function describeError(error) {
  const name = typeof error === "string" ? error : error?.name || error?.constructor?.name || "Error";
  const message = typeof error === "string" ? "" : error?.message || "";
  const description = ERROR_DESCRIPTIONS[name] || TEXT.errorFallback;
  return `${description}（${name}）${message ? `：${message}` : ""}`;
}

export function formatCaptureFailure(settings, error) {
  return interpolate(TEXT.captureUnavailable, {
    capture: formatRequestedCapture(settings),
    error: describeError(error),
  });
}

export function formatCaptureMismatch(settings) {
  return interpolate(TEXT.mismatch, { capture: formatRequestedCapture(settings) });
}

export function formatDiagnosticResult(diagnosis) {
  return TEXT.statuses[diagnosis] || TEXT.statuses["other-mismatch"];
}

export function formatConstraintProbeResult(diagnosis) {
  const labels = {
    "success-60": TEXT.probeSuccess,
    "track-60-measured-low": TEXT.probeTrackSuccessMeasuredLow,
    "actual-below-60": TEXT.probeActualBelow,
    "device-mismatch": TEXT.deviceMismatch,
    error: TEXT.probeError,
  };
  return labels[diagnosis] || TEXT.probeError;
}

export function formatCodecProbeError(error) {
  return `${TEXT.codecProbeUnavailable}（RTCRtpSender.getCapabilities）${error ? `：${error}` : ""}`;
}

function valueText(value) {
  return typeof value === "object" ? JSON.stringify(value) : String(value);
}

export function formatCapabilities(capabilities = {}) {
  const labels = {
    width: "対応幅 (width)",
    height: "対応高さ (height)",
    facingMode: "カメラの向き (facingMode)",
    resizeMode: "リサイズ方式 (resizeMode)",
    zoom: "ズーム (zoom)",
    torch: "ライト (torch)",
    focusMode: "フォーカスモード (focusMode)",
    focusDistance: "フォーカス距離 (focusDistance)",
    exposureMode: "露出モード (exposureMode)",
    exposureTime: "露出時間 (exposureTime)",
    exposureCompensation: "露出補正 (exposureCompensation)",
    whiteBalanceMode: "ホワイトバランス (whiteBalanceMode)",
    colorTemperature: "色温度 (colorTemperature)",
  };
  const lines = ["対応範囲 (getCapabilities())"];
  for (const [key, value] of Object.entries(capabilities)) {
    if (key === "frameRate" && value && typeof value === "object") {
      lines.push(`対応フレームレート範囲 (getCapabilities().frameRate)`);
      if (value.min !== undefined) lines.push(`  最小: ${value.min}`);
      if (value.max !== undefined) lines.push(`  最大: ${value.max}`);
      continue;
    }
    lines.push(`${labels[key] || key}: ${valueText(value)}`);
  }
  return lines.join("\n");
}

export function formatSettings(settings = {}) {
  const labels = {
    width: "実際の幅 (getSettings().width)",
    height: "実際の高さ (getSettings().height)",
    frameRate: "実際のフレームレート (getSettings().frameRate)",
    facingMode: "実際のカメラ向き (getSettings().facingMode)",
    deviceId: "カメラID (getSettings().deviceId)",
    resizeMode: "リサイズ方式 (getSettings().resizeMode)",
    aspectRatio: "アスペクト比 (getSettings().aspectRatio)",
  };
  const lines = ["現在の設定 (getSettings())"];
  for (const [key, value] of Object.entries(settings)) {
    lines.push(`${labels[key] || key}: ${valueText(value)}`);
  }
  return lines.join("\n");
}

export function formatConstraints(constraints = {}) {
  return `要求条件 (getConstraints())\n${JSON.stringify(constraints, null, 2)}`;
}

export function formatLatencyAPIs(apis = {}) {
  const lines = ["低遅延API対応状況（実行時検出）"];
  for (const [key, supported] of Object.entries(apis)) {
    lines.push(`${key}: ${supported ? "対応" : "非対応"}`);
  }
  return lines.join("\n");
}

export function formatMeasuredFPS(update) {
  if (!update.available) return TEXT.measurementUnavailable;
  return `${update.oneSecondFPS.toFixed(2)} ${TEXT.fpsUnit}（${TEXT.tenSecondLabel}: ${update.tenSecondFPS.toFixed(2)} ${TEXT.fpsUnit}）`;
}

export function formatStabilitySeconds(seconds) {
  const safeSeconds = Math.max(0, Math.floor(Number(seconds) || 0));
  const minutes = Math.floor(safeSeconds / 60).toString().padStart(2, "0");
  const remainder = (safeSeconds % 60).toString().padStart(2, "0");
  return `${minutes}:${remainder}`;
}

export function formatStabilityFPS(value) {
  return typeof value === "number" && Number.isFinite(value) ? `${value.toFixed(2)} fps` : TEXT.noValue;
}

export function formatStabilityStatus(status) {
  return status === "PASS" ? TEXT.stabilityPassed : TEXT.stabilityFailed;
}

export function formatWebRtcStatus(status) {
  if (status === "connecting") return TEXT.webrtcConnecting;
  if (status === "offered") return TEXT.webrtcOffered;
  if (status === "connected") return TEXT.webrtcConnected;
  if (status === "closed" || status === "disconnected") return TEXT.webrtcClosed;
  if (typeof status === "string" && status.startsWith("closed: ")) {
    return `${TEXT.webrtcClosed}（${status.slice("closed: ".length)}）`;
  }
  if (typeof status === "string" && status.startsWith("error: ")) {
    return TEXT.webrtcFailed.replace("{message}", status.slice("error: ".length));
  }
  return TEXT.webrtcIdle;
}

function formatMetric(value, suffix = "") {
  return typeof value === "number" && Number.isFinite(value) ? `${value.toFixed(2)}${suffix}` : TEXT.noValue;
}

export function formatWebRtcStats(stats = {}) {
  if (stats.error) {
    return `WebRTC送信統計の取得エラー（RTCPeerConnection.getStats）：${stats.error}`;
  }
  if (!stats.available) {
    return "WebRTC送信統計（RTCPeerConnection.getStats）：未取得";
  }
  const bitrate = typeof stats.bitrateBitsPerSecond === "number" && Number.isFinite(stats.bitrateBitsPerSecond)
    ? `${(stats.bitrateBitsPerSecond / 1_000_000).toFixed(2)} Mbps`
    : TEXT.noValue;
  return [
    "WebRTC送信統計 (RTCPeerConnection.getStats())",
    `実際のcodec: ${stats.codec || TEXT.noValue}`,
    `送信FPS (framesPerSecond): ${formatMetric(stats.framesPerSecond, " fps")}`,
    `エンコード済みフレーム (framesEncoded): ${formatMetric(stats.framesEncoded)}`,
    `送信側欠落フレーム (framesDropped): ${formatMetric(stats.framesDropped)}`,
    `bitrate (bytesSent差分): ${bitrate}`,
    `送信パケット (packetsSent): ${formatMetric(stats.packetsSent)}`,
    `packet loss (packetsLost): ${formatMetric(stats.packetsLost)}` +
      ` / ${formatMetric(stats.packetLossPercent, " %")}`,
    `RTT (roundTripTime): ${formatMetric(stats.roundTripTimeMs, " ms")}`,
    `jitter: ${formatMetric(stats.jitterMs, " ms")}`,
  ].join("\n");
}
