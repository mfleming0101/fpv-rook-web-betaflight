interface Exports {
  memory: WebAssembly.Memory
  _initialize(): void
  bf_init(): void
  bf_step(us: number): void
  bf_eeprom(): number
  bf_eeprom_size(): number
  bf_in(): number
  bf_out(): number
  bf_out_len(): number
  bf_serial(port: number, length: number): void
  bf_imu(gx: number, gy: number, gz: number, ax: number, ay: number, az: number): void
  bf_motor(index: number): number
  bf_rc(channel: number, us: number): void
  bf_rc_frame(): void
  bf_arming(): number
  bf_flight_mode(): number
  bf_held(): number
  bf_hold(): void
}

export interface Fc {
  e: Exports
  step(us: number): void
  cli(text: string): void
}

export async function load(bytes: BufferSource): Promise<Fc> {
  const module = await WebAssembly.compile(bytes)
  const fc = {} as Fc
  const boot = (saved?: Uint8Array) => {
    let memory: WebAssembly.Memory
    const fd_write = (fd: number, iovs: number, count: number, written: number) => {
      const view = new DataView(memory.buffer)
      let total = 0
      for (let i = 0; i < count; i++) total += view.getUint32(iovs + i * 8 + 4, true)
      view.setUint32(written, total, true)
      return 0
    }
    const wasi = { fd_write, fd_close: () => 0, fd_fdstat_get: () => 0, fd_seek: () => 0 }
    const e = new WebAssembly.Instance(module, { wasi_snapshot_preview1: wasi }).exports as unknown as Exports
    memory = e.memory
    e._initialize()
    if (saved) new Uint8Array(memory.buffer, e.bf_eeprom(), saved.length).set(saved)
    e.bf_init()
    fc.e = e
  }
  fc.step = (us) => {
    try {
      fc.e.bf_step(us)
    } catch (error) {
      if (!(error instanceof WebAssembly.RuntimeError)) throw error
      boot(new Uint8Array(fc.e.memory.buffer, fc.e.bf_eeprom(), fc.e.bf_eeprom_size()))
    }
  }
  fc.cli = (text) => {
    const bytes = new TextEncoder().encode(text)
    new Uint8Array(fc.e.memory.buffer, fc.e.bf_in(), bytes.length).set(bytes)
    fc.e.bf_serial(0, bytes.length)
  }
  boot()
  return fc
}

export function configure(fc: Fc, text: string) {
  fc.step(1_000_000)
  fc.cli('#')
  fc.step(200_000)
  for (const line of text.split('\n').filter(Boolean)) {
    fc.cli(`${line}\r\n`)
    fc.step(20_000)
    const out = new TextDecoder().decode(new Uint8Array(fc.e.memory.buffer, fc.e.bf_out(), fc.e.bf_out_len()))
    if (out.includes('###ERROR')) throw new Error(out.trim())
  }
}

export type Block = 'starting' | 'throttle' | 'switch' | 'other'

export interface FlightController {
  step(us: number): void
  sense(imu: ArrayLike<number>): void
  radio(us: number[]): void
  motor(index: number): number
  hold(): void
  armed(): boolean
  held(): boolean
  angle(): boolean
  blocks(): Block[]
}

const reasons: [Block, number][] = [
  ['starting', (1 << 1) | (1 << 2) | (1 << 9) | (1 << 12)],
  ['throttle', 1 << 7],
  ['switch', 1 << 25],
]

export function controller(fc: Fc, cli: string): FlightController {
  configure(fc, cli)
  const known = reasons.reduce((all, [, bits]) => all | bits, 1 << 31)
  return {
    step: fc.step,
    sense: (s) => fc.e.bf_imu(s[0], s[1], s[2], s[3], s[4], s[5]),
    radio(us) {
      us.forEach((v, i) => fc.e.bf_rc(i, v))
      fc.e.bf_rc_frame()
    },
    motor: (i) => fc.e.bf_motor(i),
    hold: () => fc.e.bf_hold(),
    armed: () => fc.e.bf_arming() >>> 31 === 1,
    held: () => !!fc.e.bf_held(),
    angle: () => !!(fc.e.bf_flight_mode() & 1),
    blocks() {
      const arming = fc.e.bf_arming()
      const found = reasons.flatMap(([name, bits]) => (arming & bits ? [name] : []))
      return arming & ~known ? [...found, 'other'] : found
    },
  }
}
