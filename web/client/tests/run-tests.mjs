import { readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";

const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const testFiles = (await readdir(testDirectory))
  .filter((fileName) => fileName.endsWith(".test.js"))
  .sort()
  .map((fileName) => path.join(testDirectory, fileName));

if (testFiles.length === 0) {
  throw new Error("No test files were found");
}

const child = spawn(process.execPath, ["--test", ...testFiles], { stdio: "inherit" });
child.on("close", (code, signal) => {
  process.exitCode = typeof code === "number" ? code : 1;
  if (signal) process.exitCode = 1;
});
