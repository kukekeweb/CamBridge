import { spawn, spawnSync } from "node:child_process";
import { promises as dns } from "node:dns";
import { createReadStream, existsSync, promises as fs, readFileSync } from "node:fs";
import { createServer } from "node:https";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createInterface } from "node:readline/promises";
import { stdin as input, stderr as output } from "node:process";
import { X509Certificate } from "node:crypto";

const SERVER_DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_WEB_ROOT = path.resolve(SERVER_DIRECTORY, "../../web/client");
const DEFAULT_PORT = 8443;
const SERVICE_NAME = "CamBridge";
const SERVICE_TYPE = "_cambridge._tcp.local.";

function usage() {
  console.log(`Usage: node server.mjs --pfx <file> --certificate <file> [options]

Options:
  --web-root <dir>  Static Web Client directory (default: ../../web/client)
  --bind <IPv4>    Private LAN IPv4 to bind (default: first detected)
  --port <port>    HTTPS port (default: ${DEFAULT_PORT})
  --no-mdns        Do not request the optional DNS-SD advertisement
  --help           Show this help`);
}

function parseArguments(argv) {
  const options = { webRoot: DEFAULT_WEB_ROOT, port: DEFAULT_PORT, mdns: true };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--help") {
      options.help = true;
    } else if (argument === "--no-mdns") {
      options.mdns = false;
    } else if (["--web-root", "--pfx", "--certificate", "--bind", "--port"].includes(argument)) {
      const value = argv[index + 1];
      if (!value || value.startsWith("--")) {
        throw new Error(`${argument} requires a value`);
      }
      index += 1;
      const key = argument.slice(2).replace("-", "");
      options[key === "webroot" ? "webRoot" : key] = key === "port" ? Number(value) : value;
    } else {
      throw new Error(`Unknown argument: ${argument}`);
    }
  }
  return options;
}

function isPrivateIPv4(address) {
  const octets = address.split(".").map(Number);
  if (octets.length !== 4 || octets.some((octet) => !Number.isInteger(octet) || octet < 0 || octet > 255)) {
    return false;
  }
  return octets[0] === 10 ||
    (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31) ||
    (octets[0] === 192 && octets[1] === 168);
}

function detectedPrivateIPv4() {
  for (const interfaces of Object.values(os.networkInterfaces())) {
    for (const network of interfaces ?? []) {
      const family = network.family === "IPv4" || network.family === 4;
      if (family && !network.internal && isPrivateIPv4(network.address)) {
        return network.address;
      }
    }
  }
  throw new Error("No private LAN IPv4 address was detected; pass --bind explicitly");
}

function certificateSanEntries(certificate) {
  return (certificate.subjectAltName ?? "")
    .split(/,\s*/)
    .map((entry) => entry.trim())
    .filter(Boolean);
}

function sanContains(entries, value) {
  return entries.some((entry) => entry === `IP Address:${value}` || entry === `IP:${value}` || entry === `DNS:${value}`);
}

function mimeType(filePath) {
  return {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
  }[path.extname(filePath).toLowerCase()] ?? "application/octet-stream";
}

async function promptForPfxPassword() {
  if (process.env.CAMBRIDGE_PFX_PASSWORD) {
    return process.env.CAMBRIDGE_PFX_PASSWORD;
  }
  const readline = createInterface({ input, output });
  try {
    return await readline.question("PFX password (input is not logged): ");
  } finally {
    readline.close();
  }
}

async function resolveLocalHostname() {
  const hostname = `${os.hostname()}.local`;
  try {
    const result = await Promise.race([
      dns.lookup(hostname, { family: 4 }),
      new Promise((_, reject) => setTimeout(() => reject(new Error("timeout")), 750)),
    ]);
    return { hostname, address: result.address };
  } catch {
    return null;
  }
}

function startMdns(port, enabled) {
  if (!enabled) {
    return { status: "disabled", child: null };
  }
  const available = spawnSync("where.exe", ["dns-sd.exe"], { stdio: "ignore" }).status === 0;
  if (!available) {
    return { status: "unavailable (dns-sd.exe not found)", child: null };
  }
  const child = spawn("dns-sd.exe", ["-R", SERVICE_NAME, "_cambridge._tcp", "local.", String(port)], {
    stdio: "ignore",
    windowsHide: true,
  });
  child.once("error", (error) => console.error(`Bonjour service error: ${error.message}`));
  child.once("close", (code) => {
    if (code !== null && code !== 0) {
      console.error(`Bonjour service stopped with exit code ${code}`);
    }
  });
  return { status: "available (DNS-SD registration requested)", child };
}

async function serveFile(request, response, webRoot) {
  let pathname;
  try {
    pathname = decodeURIComponent(new URL(request.url, "https://cambridge.invalid").pathname);
  } catch {
    response.writeHead(400);
    response.end("Bad request");
    return;
  }
  if (pathname === "/health") {
    response.writeHead(200, { "content-type": "application/json; charset=utf-8" });
    response.end(JSON.stringify({ status: "ok", stage: 1 }));
    return;
  }

  const relativePath = pathname === "/" ? "index.html" : pathname.slice(1);
  const rootWithSeparator = webRoot.endsWith(path.sep) ? webRoot : `${webRoot}${path.sep}`;
  const filePath = path.resolve(webRoot, relativePath);
  if (!filePath.startsWith(rootWithSeparator)) {
    response.writeHead(403);
    response.end("Forbidden");
    return;
  }

  try {
    const stat = await fs.stat(filePath);
    if (!stat.isFile()) {
      response.writeHead(404);
      response.end("Not found");
      return;
    }
    response.writeHead(200, { "content-type": mimeType(filePath), "cache-control": "no-store" });
    createReadStream(filePath).pipe(response);
  } catch {
    response.writeHead(404);
    response.end("Not found");
  }
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  if (options.help) {
    usage();
    return;
  }
  for (const required of ["pfx", "certificate"]) {
    if (!options[required]) {
      throw new Error(`--${required} is required`);
    }
  }
  if (!Number.isInteger(options.port) || options.port < 1 || options.port > 65535) {
    throw new Error("--port must be an integer from 1 to 65535");
  }
  const bind = options.bind ?? detectedPrivateIPv4();
  if (!isPrivateIPv4(bind)) {
    throw new Error(`Refusing non-private bind address: ${bind}`);
  }
  if (!existsSync(options.webRoot)) {
    throw new Error(`Web root does not exist: ${options.webRoot}`);
  }
  const certificate = new X509Certificate(readFileSync(options.certificate));
  const sanEntries = certificateSanEntries(certificate);
  if (!sanContains(sanEntries, bind)) {
    throw new Error(`Certificate SAN does not contain bind IPv4 ${bind}: ${sanEntries.join(", ") || "(none)"}`);
  }

  const pfxPassword = await promptForPfxPassword();
  const server = createServer({ pfx: readFileSync(options.pfx), passphrase: pfxPassword }, (request, response) => {
    serveFile(request, response, options.webRoot).catch((error) => {
      response.writeHead(500);
      response.end("Internal server error");
      console.error(`Request error: ${error.message}`);
    });
  });
  const mdns = startMdns(options.port, options.mdns);
  const localHost = await resolveLocalHostname();
  const friendlyHost = localHost && sanContains(sanEntries, localHost.hostname) ? localHost.hostname : null;

  server.on("error", (error) => console.error(`HTTPS server error: ${error.message}`));
  server.listen(options.port, bind, () => {
    console.log("CamBridge Stage 1 Server");
    console.log(`Bind address: ${bind}`);
    console.log(`HTTPS port: ${options.port}`);
    console.log(`iPhone access URL: https://${bind}:${options.port}`);
    console.log(`Certificate SAN: ${sanEntries.join(", ") || "(none)"}`);
    console.log(`mDNS / Bonjour: ${mdns.status}`);
    console.log(`Bonjour service: ${mdns.status.startsWith("available") ? `${SERVICE_NAME}.${SERVICE_TYPE}` : "unavailable"}`);
    console.log(`.local hostname: ${friendlyHost ?? "unavailable"}`);
    console.log(`Friendly URL: ${friendlyHost ? `https://${friendlyHost}:${options.port}` : "unavailable"}`);
    console.log("Stage 1: no WebRTC media transport is enabled");
  });

  const shutdown = () => {
    mdns.child?.kill();
    server.close(() => process.exit(0));
  };
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}

main().catch((error) => {
  console.error(`CamBridge Stage 1 server failed: ${error.message}`);
  process.exitCode = 1;
});
