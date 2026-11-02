#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"

#include "drivers/io.h"
#include "drivers/motor.h"
#include "drivers/system.h"
#include "drivers/pwm_output.h"
#include "drivers/time.h"
#include "drivers/accgyro/accgyro_virtual.h"
#include "config/config.h"
#include "fc/core.h"
#include "fc/init.h"
#include "fc/rc_modes.h"
#include "fc/runtime_config.h"
#include "flight/imu.h"
#include "flight/pid.h"
#include "rx/rx.h"
#include "scheduler/scheduler.h"

#define EXPORT(name) __attribute__((export_name(#name)))

uint32_t SystemCoreClock;

static uint64_t nowUs;

static float rcChannels[SIMULATOR_MAX_RC_CHANNELS];
static bool rcFresh;
static int16_t gyroIn[3];
static int16_t accIn[3];
static float motorOut[MAX_SUPPORTED_MOTORS];
static bool held;
static bool raised;

void systemInit(void)
{
    SystemCoreClock = 500 * 1e6;
}

void systemReset(void)
{
    printf("[system]Reset!\n");
    __builtin_trap();
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    UNUSED(requestType);
    systemReset();
}

void timerInit(void)
{
}

void failureMode(failureMode_e mode)
{
    printf("[failureMode]!!! %d\n", mode);
    __builtin_trap();
}

void indicateFailure(failureMode_e mode, int repeatCount)
{
    UNUSED(repeatCount);
    printf("Failure LED flash for: [failureMode]!!! %d\n", mode);
}

uint64_t nanos64_real(void) { return nowUs * 1000; }
uint64_t micros64_real(void) { return nowUs; }
uint64_t millis64_real(void) { return nowUs / 1000; }
uint64_t micros64(void) { return nowUs; }
uint64_t millis64(void) { return nowUs / 1000; }
uint32_t micros(void) { return (uint32_t)nowUs; }
uint32_t millis(void) { return (uint32_t)(nowUs / 1000); }

int32_t clockCyclesToMicros(int32_t clockCycles) { return clockCycles; }
int32_t clockCyclesTo10thMicros(int32_t clockCycles) { return clockCycles; }
int32_t clockCyclesTo100thMicros(int32_t clockCycles) { return clockCycles; }
uint32_t clockMicrosToCycles(uint32_t us) { return us; }

uint32_t getCycleCounter(void)
{
    return (uint32_t)nowUs++;
}

void delayMicroseconds(uint32_t us) { nowUs += us; }
void delayMicroseconds_real(uint32_t us) { nowUs += us; }
void delay(uint32_t ms) { nowUs += (uint64_t)ms * 1000; }

pwmOutputPort_t motors[MAX_SUPPORTED_MOTORS];

void servoDevInit(const servoDevConfig_t *servoConfig)
{
    UNUSED(servoConfig);
}

void pwmWriteServo(uint8_t index, float value)
{
    UNUSED(index);
    UNUSED(value);
}

pwmOutputPort_t *pwmGetMotors(void)
{
    return motors;
}

bool pwmIsMotorEnabled(uint8_t index)
{
    return motors[index].enabled;
}

static motorDevice_t motorPwmDevice;

static float pwmConvertFromExternal(uint16_t externalValue) { return (float)externalValue; }
static uint16_t pwmConvertToExternal(float motorValue) { return (uint16_t)motorValue; }
static void pwmDisableMotors(void) { motorPwmDevice.enabled = false; }
static bool pwmEnableMotors(void) { motorPwmDevice.enabled = true; return true; }
static void pwmWriteMotor(uint8_t index, float value) { if (index < MAX_SUPPORTED_MOTORS) motorOut[index] = value; }
static void pwmWriteMotorInt(uint8_t index, uint16_t value) { pwmWriteMotor(index, (float)value); }
static void pwmShutdownPulsesForAllMotors(void) { motorPwmDevice.enabled = false; }
static void pwmCompleteMotorUpdate(void) { }

static motorDevice_t motorPwmDevice = {
    .vTable = {
        .postInit = motorPostInitNull,
        .convertExternalToMotor = pwmConvertFromExternal,
        .convertMotorToExternal = pwmConvertToExternal,
        .enable = pwmEnableMotors,
        .disable = pwmDisableMotors,
        .isMotorEnabled = pwmIsMotorEnabled,
        .decodeTelemetry = motorDecodeTelemetryNull,
        .write = pwmWriteMotor,
        .writeInt = pwmWriteMotorInt,
        .updateComplete = pwmCompleteMotorUpdate,
        .shutdown = pwmShutdownPulsesForAllMotors,
    }
};

motorDevice_t *motorPwmDevInit(const motorDevConfig_t *motorConfig, uint16_t idlePulse, uint8_t motorCount, bool useUnsyncedPwm)
{
    UNUSED(motorConfig);
    UNUSED(idlePulse);
    UNUSED(useUnsyncedPwm);
    for (int i = 0; i < MAX_SUPPORTED_MOTORS && i < motorCount; i++) {
        motors[i].enabled = true;
    }
    motorPwmDevice.count = motorCount;
    motorPwmDevice.initialized = true;
    motorPwmDevice.enabled = false;
    return &motorPwmDevice;
}

uint16_t adcGetChannel(uint8_t channel)
{
    UNUSED(channel);
    return 0;
}

char _estack;
char _Min_Stack_Size;

void FLASH_Unlock(void) { }
void FLASH_Lock(void) { }
FLASH_Status FLASH_ErasePage(uintptr_t Page_Address) { UNUSED(Page_Address); return FLASH_COMPLETE; }

FLASH_Status FLASH_ProgramWord(uintptr_t addr, uint32_t value)
{
    if (addr >= (uintptr_t)eepromData && addr < (uintptr_t)ARRAYEND(eepromData)) {
        *((uint32_t *)addr) = value;
    }
    return FLASH_COMPLETE;
}

void IOConfigGPIO(IO_t io, ioConfig_t cfg) { UNUSED(io); UNUSED(cfg); }
void spektrumBind(rxConfig_t *rxConfig) { UNUSED(rxConfig); }
void debugInit(void) { }
void unusedPinsInit(void) { }
void IOHi(IO_t io) { UNUSED(io); }
void IOLo(IO_t io) { UNUSED(io); }

static float readRC(const rxRuntimeState_t *state, uint8_t channel)
{
    UNUSED(state);
    return rcChannels[channel];
}

static uint8_t rcFrameStatus(rxRuntimeState_t *state)
{
    UNUSED(state);
    if (!rcFresh) {
        return RX_FRAME_PENDING;
    }
    rcFresh = false;
    return RX_FRAME_COMPLETE;
}

EXPORT(bf_init) void bfInit(void)
{
    init();
}

EXPORT(bf_step) void bfStep(uint32_t us)
{
    const uint64_t end = nowUs + us;
    while (nowUs < end) {
        if (virtualGyroDev) {
            virtualGyroSet(virtualGyroDev, gyroIn[0], gyroIn[1], gyroIn[2]);
        }
        if (virtualAccDev) {
            virtualAccSet(virtualAccDev, accIn[0], accIn[1], accIn[2]);
        }
        scheduler();
    }
}

EXPORT(bf_imu) void bfImu(int16_t gx, int16_t gy, int16_t gz, int16_t ax, int16_t ay, int16_t az)
{
    gyroIn[0] = gx;
    gyroIn[1] = gy;
    gyroIn[2] = gz;
    accIn[0] = ax;
    accIn[1] = ay;
    accIn[2] = az;
}

EXPORT(bf_euler) int16_t bfEuler(uint8_t axis)
{
    return attitude.raw[axis];
}

EXPORT(bf_rc) void bfRc(uint8_t channel, float us)
{
    rcChannels[channel] = us;
}

static bool armedBy(uint8_t aux, float us)
{
    for (int i = 0; i < MAX_MODE_ACTIVATION_CONDITION_COUNT; i++) {
        const modeActivationCondition_t *mac = modeActivationConditions(i);
        if (mac->modeId == BOXARM && mac->auxChannelIndex == aux && IS_RANGE_USABLE(&mac->range) && us >= MODE_STEP_TO_CHANNEL_VALUE(mac->range.startStep) && us < MODE_STEP_TO_CHANNEL_VALUE(mac->range.endStep)) {
            return true;
        }
    }
    return false;
}

static bool holdArm(void)
{
    bool any = false;
    for (int i = 0; i < MAX_MODE_ACTIVATION_CONDITION_COUNT; i++) {
        const modeActivationCondition_t *mac = modeActivationConditions(i);
        if (mac->modeId != BOXARM || !IS_RANGE_USABLE(&mac->range)) {
            continue;
        }
        for (float us = CHANNEL_RANGE_MIN + 12.5f; us < CHANNEL_RANGE_MAX; us += 25) {
            if (!armedBy(mac->auxChannelIndex, us)) {
                rcChannels[NON_AUX_CHANNEL_COUNT + mac->auxChannelIndex] = us;
                any = true;
                break;
            }
        }
    }
    return any;
}

static bool switchedOn(void)
{
    for (int i = 0; i < MAX_MODE_ACTIVATION_CONDITION_COUNT; i++) {
        const modeActivationCondition_t *mac = modeActivationConditions(i);
        if (mac->modeId == BOXARM && armedBy(mac->auxChannelIndex, rcChannels[NON_AUX_CHANNEL_COUNT + mac->auxChannelIndex])) {
            return true;
        }
    }
    return false;
}

EXPORT(bf_hold) void bfHold(void)
{
    held = true;
    raised = false;
    imuSetAttitudeQuat(1, 0, 0, 0);
}

EXPORT(bf_held) bool bfHeld(void)
{
    return held;
}

EXPORT(bf_rc_frame) void bfRcFrame(void)
{
    if (held) {
        const bool high = getArmingDisableFlags() & ARMING_DISABLED_THROTTLE;
        const bool lowered = raised && !high;
        raised = raised || (!ARMING_FLAG(ARMED) && high);
        held = switchedOn() && !lowered;
    }
    if (held && !holdArm()) {
        disarm(DISARM_REASON_SWITCH);
    }
    rxRuntimeState.channelCount = SIMULATOR_MAX_RC_CHANNELS;
    rxRuntimeState.rcReadRawFn = readRC;
    rxRuntimeState.rcFrameStatusFn = rcFrameStatus;
    rxRuntimeState.rxProvider = RX_PROVIDER_UDP;
    rcFresh = true;
}

EXPORT(bf_motor) float bfMotor(uint8_t index)
{
    return motorOut[index];
}

EXPORT(bf_eeprom) uint8_t *bfEeprom(void)
{
    return eepromData;
}

EXPORT(bf_eeprom_size) uint32_t bfEepromSize(void)
{
    return sizeof(eepromData);
}

EXPORT(bf_arming) uint32_t bfArming(void)
{
    return ARMING_FLAG(ARMED) ? 0x80000000u : getArmingDisableFlags();
}

EXPORT(bf_flight_mode) uint32_t bfFlightMode(void)
{
    return flightModeFlags;
}

EXPORT(bf_angle_limit) void bfAngleLimit(uint8_t degrees)
{
    currentPidProfile->angle_limit = degrees;
}

#include "pg/pg.h"

EXPORT(bf_pg_count) uint32_t bfPgCount(void)
{
    return PG_REGISTRY_SIZE;
}

EXPORT(bf_pg_bad) uint32_t bfPgBad(void)
{
    uint32_t bad = 0;
    PG_FOREACH(reg) {
        if (pgFind(pgN(reg)) != reg || reg->address == NULL || pgSize(reg) == 0) {
            bad++;
        }
    }
    return bad;
}
