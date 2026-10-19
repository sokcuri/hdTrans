import type { WasmModule } from './runtime.js';

export interface EngineModule extends WasmModule {
  FS: {
    mkdir(path: string): void;
    writeFile(path: string, data: Uint8Array): void;
  };
}

export default function createEngine(options: { wasmBinary: Uint8Array }): Promise<EngineModule>;
