#!/usr/bin/env node
import { readFile, readdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { parseArgs } from 'node:util';

const help = `hdTrans
Author: Sokcuri (https://sokcuri.neko.kr/)

Usage: hdtrans [options] [text]

Translate Japanese to Korean. Without text, read UTF-8 stdin.

  -i, --in PATH         Read a UTF-8 file (- for stdin; --file also accepted)
  -o, --out PATH        Write UTF-8 output to a file (- for stdout)
  -j, --json            Output text, choices and logs as JSON
      --why             Write execution logs and WHY choice records to stderr
      --userdict PATH   Read one UTF-8 native user dictionary file
      --ehnd DIR        Read UTF-8 Ehnd .txt files from a directory
      --wrapped         Input lines break mid-sentence (captions, hooked text); the line count is kept
      --context PATH    Read UTF-8 lines before the input as context, not output
  -h, --help            Show help
  -v, --version         Load and show engine and dictionary versions
`;

async function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      in: { type: 'string', short: 'i' },
      file: { type: 'string' },
      out: { type: 'string', short: 'o' },
      json: { type: 'boolean', short: 'j' },
      why: { type: 'boolean' },
      userdict: { type: 'string', multiple: true },
      ehnd: { type: 'string' },
      wrapped: { type: 'boolean' },
      context: { type: 'string' },
      help: { type: 'boolean', short: 'h' },
      version: { type: 'boolean', short: 'v' },
    },
  });
  if (values.help) {
    process.stdout.write(help);
    return;
  }
  if ((values.userdict?.length ?? 0) > 1) throw new Error('only one --userdict file is supported');
  if (values.in !== undefined && values.file !== undefined) {
    throw new Error('use either --in or --file, not both');
  }
  const file = values.in ?? values.file;
  if (file !== undefined && positionals.length) {
    throw new Error('use either --in or text, not both');
  }
  if (!values.version && file === undefined && !positionals.length && process.stdin.isTTY) {
    process.stdout.write(help);
    return;
  }
  const ehnd: Record<string, string> = {};
  if (values.ehnd) {
    for (const entry of await readdir(values.ehnd, { withFileTypes: true })) {
      if (entry.isFile() && /^(PreFilter|PostFilter|SkipLayer|UserDict)[^/\\\0]*\.txt$/i.test(entry.name)) {
        ehnd[entry.name] = await readFile(join(values.ehnd, entry.name), 'utf8');
      }
    }
  }
  const { HdTrans } = await import('./index.js');
  const translator = await HdTrans.init({ ehnd, wrapped: values.wrapped ?? false, strictDict: false });
  try {
    if (values.version) {
      process.stdout.write(`hdTrans v${translator.version.replace(/^v/, '')} (${translator.runtime}) / dict ${translator.dictionaryVersion}\n`);
      return;
    }
    if (values.userdict) {
      translator.setUserDictRaw(await readFile(values.userdict[0], 'utf8'));
      if (translator.userDictError) process.stderr.write(`${translator.userDictError}\n`);
    }
    for (const diagnostic of translator.filterErrors) process.stderr.write(`${diagnostic}\n`);
    const input = positionals.length
      ? positionals.join(' ')
      : file && file !== '-'
        ? await readFile(file, 'utf8')
        : await readStdin();
    const context = values.context === undefined
      ? []
      : (await readFile(values.context, 'utf8')).replace(/\n$/, '').split('\n');
    const result = values.json || values.why ? translator.translate(input, { json: true, context }) : undefined;
    if (values.why && result) {
      for (const message of result.logs) process.stderr.write(message);
      for (const choice of result.choices) process.stderr.write(`WHY\t${JSON.stringify(choice)}\n`);
    }
    const document = values.json && result
      ? `${JSON.stringify(result)}\n`
      : result?.text ?? translator.translate(input, { context });
    if (values.out && values.out !== '-') await writeFile(values.out, document, 'utf8');
    else process.stdout.write(document);
  } finally {
    translator.close();
  }
}

async function readStdin() {
  process.stdin.setEncoding('utf8');
  let text = '';
  for await (const chunk of process.stdin) text += chunk;
  return text;
}

main().catch((error: unknown) => {
  process.stderr.write(`hdtrans: ${error instanceof Error ? error.message : String(error)}\n`);
  process.exitCode = 1;
});
