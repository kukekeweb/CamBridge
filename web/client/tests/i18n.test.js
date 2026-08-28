import test from "node:test";
import assert from "node:assert/strict";
import {
  describeError,
  formatActualCapture,
  formatCodecProbeError,
  formatDiagnosticResult,
  formatOutputPlan,
  formatRequestedCapture,
} from "../src/i18n.js";

test("user-facing capture request is Japanese while retaining exact values", () => {
  assert.equal(
    formatRequestedCapture({ resolution: { width: 1920, height: 1080 }, frameRate: 60 }),
    "1920×1080 / 60fps",
  );
});

test("capture diagnostics keep display formatting centralized", () => {
  assert.equal(formatActualCapture({ width: 1920, height: 1080, frameRate: 30 }), "1920×1080 / 30fps");
  assert.equal(
    formatOutputPlan({ outputDimensions: { width: 1080, height: 1920 }, rotationDegrees: 90, transportTransform: "rotate-90" }),
    "1080×1920 / 回転 90° / transform: rotate-90",
  );
});

test("known camera errors include Japanese guidance and original error name", () => {
  assert.equal(
    describeError({ name: "NotAllowedError", message: "Permission denied" }),
    "カメラの使用が許可されていません（NotAllowedError）：Permission denied",
  );
  assert.equal(
    describeError({ name: "OverconstrainedError", message: "width" }),
    "指定したカメラ設定を満たせません（OverconstrainedError）：width",
  );
});

test("diagnostic result uses explicit Japanese category text for A, B, C, and errors", () => {
  assert.match(formatDiagnosticResult("A"), /^非対応/);
  assert.match(formatDiagnosticResult("B"), /^不一致/);
  assert.match(formatDiagnosticResult("C"), /^不一致/);
  assert.equal(formatDiagnosticResult("match"), "成功");
  assert.equal(formatDiagnosticResult("getUserMedia-failed"), "エラー");
});

test("codec probe failures remain Japanese while retaining the API name and raw reason", () => {
  assert.equal(
    formatCodecProbeError("API unavailable"),
    "対応コーデックAPIを利用できません（RTCRtpSender.getCapabilities）：API unavailable",
  );
});
