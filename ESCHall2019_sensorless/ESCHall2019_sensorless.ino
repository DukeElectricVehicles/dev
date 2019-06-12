#define useCANx
#define useI2Cx
#define useINA226  // warning: this used to be not working but then it mysteriously started working again, may stop working at some point
#define useINA332x
#define useHallSpeedx
#define useWatchdogx
#define COMPLEMENTARYPWMx
#define DEV
#define SENSORLESS
#define ADCBODGEx
#define OC_LIMIT 1.0 // current limit
#define useTRIGDELAYCOMPENSATION

#define NUMPOLES 16

#define PERIODSLEWLIM_US_PER_S 50000
#define MAXCURRENT 20

#if defined(useCAN) && !defined(__MK20DX256__)
  #error "Teensy 3.2 required for CAN"
#endif
#if defined(useCAN) && defined(useI2C)
  #error "strongly discourage using CAN and I2C at the same time"
#endif

#include <i2c_t3.h>
// #include "TimerOne.h"
#include "config.h"
#include "MCpwm_2019sensorless.h"
#include "CANCommands.h"
#include "Metro.h"
#include "est_BEMF_delay.h"
#include "est_hall_simple.h"
#include "controller_current.h"

#ifdef useINA226
  #include "INA.h"
#endif

#ifdef useHallSpeed
  #define DIST_PER_TICK 0.19948525 // (20 in) * pi / (8 ticks/rev) = (0.199 m/tick)
  uint32_t lastTime_hallSpeed_us = 0;
  float hallSpeed_alpha = .95;
  float hallSpeed_prev_mps = 0;
  float hallSpeed_LPF_mps = 0;
  void hallSpeedISR(){
    uint32_t newTime = micros();
    hallSpeed_prev_mps = DIST_PER_TICK * 1e6 / (newTime - lastTime_hallSpeed_us);
    lastTime_hallSpeed_us = newTime;
  }
#endif

uint32_t lastTime_throttle = 0;
uint32_t lastTime_I2C = 0;
uint32_t lastTime_CAN = 0;
float lastDuty_UART = 0;
Metro checkFaultTimer(100);
bool FAULT = false;
Metro printTimer(2500);
volatile uint8_t recentWriteState;
uint32_t sensorlessTimeout;
uint32_t hallEnTimeout;

float throttle = 0;
volatile uint16_t duty = 0;
volatile bool dir = true; // true = forwards

volatile commutateMode_t commutateMode = MODE_HALL;
inputMode_t inputMode = INPUT_THROTTLE;
#ifdef SENSORLESS
  bool useSensorless = true;
#else
  bool useSensorless = false;
#endif
pwmMode_t pwmMode = PWM_NONSYNCHRONOUS;
bool autoSynchronous = true;

volatile uint32_t timeToUpdateCmp = 0;

volatile uint32_t period_commutation_usPerTick = 1e6;
volatile int16_t phaseAdvance_Q10 = 205; // 0.2

controlMode_t controlMode = CONTROL_CURRENT_CLOSEDLOOP;
float Kv = 25.5; // Mitsuba
float Rs = 0.25;
// float Kv = 188; // Koford
// float Rs = 0.2671;
float maxCurrent = 5, minCurrent = 0;
float rpm = 0, Vbus = 0;

void setup(){
  setupWatchdog();
  Vbus = getBusVoltage();
  setupPWM();
  setupPins();
  setup_hall();
  #ifdef SENSORLESS
    setupADC();
  #endif
  kickDog();
  #ifdef useCAN
    setupCAN();
  #endif
  #ifdef useINA226
    INAinit();
  #endif

  kickDog();

  #ifdef useHallSpeed
    attachInterrupt(HALL_SPEED, hallSpeedISR, RISING);
  #endif
  pinMode(0, OUTPUT);
  pinMode(1, OUTPUT);

  printHelp();
}

void loop(){
  static uint16_t loopCounter = 0;
  
  loopCounter++;
  uint32_t curTime = millis();
  uint32_t curTimeMicros = micros();
  if (curTimeMicros > timeToUpdateCmp) {
    timeToUpdateCmp = curTimeMicros + 1000;
    updateCmp_BEMFdelay();
  }
  // if ((!delayCommutateFinished) && (micros() >= delayCommutateTimer)){
  //   delayCommutate_isr();
  // }

  // updateBEMFdelay(curTimeMicros);

  #ifdef useCAN
    getThrottle_CAN();
  #endif

  // if (checkFaultTimer.check()){
  // }

  // if ((curTimeMicros % 100) > 90) {
  //   hallEn = true;
  //   digitalWriteFast(HALLEN,HIGH);
  // } else {
  //   hallEn = false;
  //   digitalWriteFast(HALLEN,LOW);
  // }

  if (leftSensorless) {
    leftSensorless = false;
    sensorlessTimeout = millis();
    digitalWrite(13, commutateMode != MODE_HALL);
    Serial.print("BEMF crossing appears to have been missed\t");
    Serial.print('\n');
  }

  curTimeMicros = micros();
  commutateMode_t tmp = commutateMode;
  if ((millis()-hallEnTimeout) > 1000) {
    hallEn = false;
    digitalWriteFast(HALLEN, hallEn);
  }
  if (rpm < 230) {
    hallEn = true;
    hallEnTimeout = millis();
    digitalWriteFast(HALLEN, hallEn);
  }

  switch (tmp) {
    case MODE_SENSORLESS_DELAY: {
      if (hallEn) {
        uint32_t percentSpeedSenseDiff = 100*period_bemfdelay_usPerTick/period_hallsimple_usPerTick;
        if ((percentSpeedSenseDiff < 60) || (percentSpeedSenseDiff > 140)){
          commutateMode = MODE_HALL;
          hallnotISR();
          digitalWrite(13, commutateMode != MODE_HALL);
          Serial.println("Transitioned out of sensorless");
          sensorlessTimeout = millis();
        }
      }
      break;
    }
    case MODE_HALL: {
      hallEnTimeout = millis();
      uint32_t percentSpeedSenseDiff = 100*period_bemfdelay_usPerTick/period_hallsimple_usPerTick;
      if (useSensorless && (percentSpeedSenseDiff > 90) && (percentSpeedSenseDiff < 110) && ((millis()-sensorlessTimeout) > 100)){
        commutateMode = MODE_SENSORLESS_DELAY;
        digitalWrite(13, commutateMode != MODE_HALL);
        Serial.println("Transitioned into sensorless");
      }
      break;
    }
    default: {
      commutateMode = MODE_HALL;
      break;
    }
  }

  // if ((period_commutation_usPerTick < 3000) && (commutateMode == MODE_HALL)){
  //   commutateMode = MODE_SENSORLESS_DELAY;
  //   // digitalWriteFast(13, HIGH);
  // } else if ((period_commutation_usPerTick > 3500) && (commutateMode == MODE_SENSORLESS_DELAY)){
  //   commutateMode = MODE_HALL;
  //   // digitalWriteFast(13, LOW);
  // }

  // hallnotISR();

  // delayMicroseconds(100);

  if (curTime - lastTime_throttle > 5)
  {
    // #ifdef useI2C
    // if (curTime - lastTime_I2C < 300){
    //   throttle = getThrottle_I2C();
    //   if (throttle == 0)
    //     throttle = getThrottle_analog() * 4095;
    // } else {
    //   throttle = getThrottle_analog() * 4095;
    // }
    // #elif defined(useCAN)
    // throttle = MODULO*pow(getThrottle_CAN()/MODULO.0,3);
    // if ((curTime - lastTime_CAN > 300) || (throttle==0)){
    //   throttle = getThrottle_analog() * MODULO;
    // }
    // #else

    rpm += 0.02*(60e6*1.0/period_commutation_usPerTick/6/NUMPOLES - rpm);
    rpm = constrain(rpm, 0, 6000);
    // if (duty > (0.15*MODULO))
    //   Vbus = 3.3 * vsx_cnts[highPhase] / (1<<ADC_RES_BITS) * (39.2e3 / 3.32e3) * 16.065/14.77;
    Vbus = InaVoltage_V;

    if (autoSynchronous) {
      switch(pwmMode) {
        case PWM_COMPLEMENTARY:
          if ((InaCurrent_A < 1) && (Isetpoint>=0))
            pwmMode = PWM_NONSYNCHRONOUS;
          break;
        case PWM_NONSYNCHRONOUS:
          if ((InaCurrent_A > 1.5) || (Isetpoint<0))
            pwmMode = PWM_COMPLEMENTARY;
          break;
      }
    }

    switch (inputMode) {
      case INPUT_THROTTLE:
        throttle = getThrottle_analog(); // * MODULO;
        break;
      case INPUT_UART:
        throttle = lastDuty_UART; // * MODULO;
        break;
      default:
        throttle = 0;
        break;
    }

    switch (controlMode) {
      float Iset;
      case CONTROL_DUTY:
        duty = constrain(throttle * MODULO, 0, MODULO);
        break;
      case CONTROL_CURRENT_OPENLOOP:
        Iset = map(throttle, 0, 1, minCurrent, maxCurrent);
        // I = (Vbus*D - rpm/Kv) / Rs
        duty = constrain((uint16_t)((Iset*Rs + rpm/Kv) / Vbus * MODULO), 0, MODULO);
        break;
      case CONTROL_CURRENT_CLOSEDLOOP:
        Iset = map(throttle, 0, 1, minCurrent, maxCurrent);
        if (throttle <= 0.01) {
          duty = 0;
        } else {
          Iset = constrain(Iset, minCurrent, maxCurrent);
          setSetpoint_I(Iset);
          duty = constrain(getPIDvoltage_I(InaCurrent_A)/Vbus * MODULO, 0, MODULO);
        }
        break;
    }
    // #endif

    #ifdef useHallSpeed
      float hallSpeed_tmp = min(hallSpeed_prev_mps,
                                DIST_PER_TICK * 1e6 / (micros()-lastTime_hallSpeed_us)); // allows vel to approach 0
      hallSpeed_LPF_mps = (hallSpeed_alpha)*hallSpeed_LPF_mps + (1-hallSpeed_alpha)*hallSpeed_tmp;
    #endif

    #ifdef useINA226
      updateINA();
    #endif

    hallnotISR();
    lastTime_throttle = curTime;

    kickDog();

    digitalWrite(13, commutateMode != MODE_HALL);

    readSerial();
  }
  if (ADCsampleDone) {
    Serial.println("********************************");
    for (uint16_t i = 0; i<ADCSAMPLEBUFFERSIZE; i++) {
      for (uint16_t j = 0; j<(sizeof(vsxSamples_cnts[0])/sizeof(vsxSamples_cnts[0][0])); j++) {
        Serial.print(vsxSamples_cnts[i][j]); Serial.print('\t');
      }
      Serial.println();
    }
    Serial.println("********************************");
    ADCsampleDone = false;
  }

  if (printTimer.check()) {
    printDebug(curTime);
  }
}

void printDebug(uint32_t curTime) {
  Serial.print(curTime);
  Serial.print('\t');
  static double InaCurrent_A_LPF = 0;
  static double rpm_LPF = 0;
  InaCurrent_A_LPF += 0.03*(InaCurrent_A - InaCurrent_A_LPF);
  rpm_LPF += 0.1*(rpm-rpm_LPF);
  rpm_LPF = constrain(rpm_LPF, 0, 500);
  #ifdef useINA226
    Serial.print(InaVoltage_V);
    Serial.print('\t');
    Serial.print(InaCurrent_A_LPF,4);
    Serial.print('\t');
    Serial.print(InaPower_W);
    Serial.print('\t');
    Serial.print(InaEnergy_J);
    Serial.print('\t');
  #endif
  Serial.print(throttle);
  Serial.print('\t');
  Serial.print(duty);
  Serial.print('\t');
  // Serial.print(Isetpoint);
  // Serial.print('\t');
  // Serial.print(uP);
  // Serial.print('\t');
  // Serial.print(uI);
  // Serial.print('\t');
  Serial.print(rpm_LPF);
  Serial.print('\t');
  Serial.print(period_hallsimple_usPerTick);
  Serial.print('\t');
  // Serial.print(delayCommutateTimer-micros());
  // Serial.print('\t');
  Serial.print(period_bemfdelay_usPerTick);
  Serial.print('\t');
  Serial.print(period_commutation_usPerTick);
  Serial.print('\t');
  Serial.print(vsx_cnts[0]);
  Serial.print('\t');
  Serial.print(vsx_cnts[1]);
  Serial.print('\t');
  Serial.print(vsx_cnts[2]);
  Serial.print('\t');

  #ifdef useHallSpeed
  Serial.print('\t');
  Serial.print(digitalRead(HALL_SPEED));
  Serial.print('\t');
  Serial.print('\t');
  Serial.print(hallSpeed_LPF_mps,3);
  #endif

  Serial.print(Serial.availableForWrite());
  // Serial.print('\t');
  Serial.print('\n');
}
void printHelp() {
  Serial.println("*******************");
  Serial.println("* SERIAL COMMANDS *");
  Serial.println("*******************");
  Serial.println("h - print this help menu");
  Serial.println("r - clear a fault condition");
  Serial.print  ("u - switch to UART controlled throttle "); Serial.println((inputMode==INPUT_UART ? "*" : ""));
  Serial.print  ("t - switch to ADC controlled throttle "); Serial.println((inputMode==INPUT_THROTTLE ? "*" : ""));
  Serial.print  ("i - switch to I2C controlled throttle "); Serial.println((inputMode==INPUT_I2C ? "*" : ""));
  Serial.print  ("c - switch to CAN controlled throttle "); Serial.println((inputMode==INPUT_CAN ? "*" : ""));
  Serial.print  ("s - toggle between synchronous");     Serial.print((pwmMode==PWM_COMPLEMENTARY?"*":""));  Serial.print(" vs nonsynchronous switching"); Serial.println((pwmMode==PWM_NONSYNCHRONOUS?"*":""));
  Serial.print  ("@ - toggle between auto-synchronous");Serial.print((autoSynchronous?"*":""));             Serial.print(" and manual");                  Serial.println((!autoSynchronous?"*":""));
  Serial.print  ("S - toggle between sensorless");      Serial.print((useSensorless?"*":""));               Serial.print(" and no sensorless");           Serial.println((!useSensorless?"*":""));
  Serial.print  ("P - switch to duty cycle control "); Serial.println((controlMode==CONTROL_DUTY?"*":""));
  Serial.print  ("I - switch to current control (closed loop) "); Serial.println((controlMode==CONTROL_CURRENT_CLOSEDLOOP?"*":""));
  Serial.print  ("l - switch to current control (open loop) (lower case L) "); Serial.println((controlMode==CONTROL_CURRENT_OPENLOOP?"*":""));
  Serial.print  ("H - switch the hall sensors on/off (currently "); Serial.print(hallEn?"on":"off"); Serial.println(")");
  Serial.println("d - specify the interval to print debug data (less than 8ms risks taking too much CPU time)");
  Serial.println("o - sample the ADC data super fast");
  Serial.println("D# - set the duty cycle to # if in UART throttle mode (i.e. D0.3 sets 30% duty cycle)");
  Serial.println("A# - set the current setpoint to # amps");
  Serial.println("a# - set the phase advance to # percent when in sensorless (i.e. a30 sets the phase advance to 30%)");
  Serial.print  ("g# - set kP = # (currently ");Serial.print(String(kP,3));Serial.println(")");
  Serial.print  ("G# - set kI = # (currently ");Serial.print(String(kI,3));Serial.println(")");
  Serial.print  ("K# - set Kv = # (currently ");Serial.print(String(Kv,3));Serial.println(")");
  Serial.print  ("R# - set Rs = # (currently ");Serial.print(String(Rs,3));Serial.println(")");
  Serial.println("-------------------");
}
void readSerial() {
  if (Serial.available()){
    char input = Serial.read();
    float valInput;
    switch (input) {
      case 'h':
        printHelp();
        break;
      case 'r':
        FAULT = false;
        break;
      case 'u':
        inputMode = INPUT_UART;
        break;
      case 't':
        inputMode = INPUT_THROTTLE;
        break;
      case 'i':
        inputMode = INPUT_I2C;
        break;
      case 'c':
        inputMode = INPUT_CAN;
        break;
      case 's':
        switch(pwmMode) {
          case PWM_COMPLEMENTARY:
            pwmMode = PWM_NONSYNCHRONOUS;
            break;
          case PWM_NONSYNCHRONOUS:
            pwmMode = PWM_COMPLEMENTARY;
            break;
        }
        break;
      case '@':
        autoSynchronous = !autoSynchronous;
        break;
      case 'S':
        useSensorless = !useSensorless;
        if (!useSensorless){
          commutateMode = MODE_HALL;
        }
        break;
      case 'l':
        controlMode = CONTROL_CURRENT_OPENLOOP;
        break;
      case 'H':
        hallEn = !hallEn;
        digitalWriteFast(HALLEN, hallEn);
      case 'I':
        bumplessPIDupdate_I((float)duty / MODULO, InaCurrent_A);
        controlMode = CONTROL_CURRENT_CLOSEDLOOP;
        break;
      case 'P':
        controlMode = CONTROL_DUTY;
        Isetpoint = 0;
        setSetpoint_I(0);
        break;
      case 'd':
        printTimer.interval(Serial.parseInt());
        break;
      case 'o':
        ADCsampleCollecting = true;
        break;
      case 'D':
        if (controlMode == CONTROL_DUTY) {
          valInput = Serial.parseFloat();
          dir = (valInput >= 0);
          valInput = fabsf(valInput);
          lastDuty_UART = constrain(valInput, 0, 1);
        }
        break;
      case 'A':
        if (controlMode == CONTROL_CURRENT_CLOSEDLOOP) {
          lastDuty_UART = constrain(map(Serial.parseFloat(), minCurrent, maxCurrent, 0, 1000)/1000.0, -1, 1);
        }
        break;
      case 'a':
        valInput = Serial.parseFloat();
        phaseAdvance_Q10 = constrain(valInput, -100, 100) / 100.0 * (1<<10);
        break;
      case 'g':
        kP = Serial.parseFloat();
        break;
      case 'G':
        kI = Serial.parseFloat();
        break;
      case 'K':
        Kv = Serial.parseFloat();
        break;
      case 'R':
        Rs = Serial.parseFloat();
        break;
    }
  }
}

void exitSensorless() {
  hallEn = true;
  hallEnTimeout = millis();
  digitalWriteFast(HALLEN, hallEn);
  if (commutateMode == MODE_SENSORLESS_DELAY) {
    commutateMode = MODE_HALL;
    hallnotISR();
    leftSensorless = true;
  }
}

void INAOC_isr() {
  // writeTrap(0, -1);
  // FAULT = true;
}
void commutate_isr(uint8_t phase, commutateMode_t caller) {
  if (caller != commutateMode){
    // Serial.print("tried to commutate by ");
    // Serial.println(caller);
    return;
  }
  static uint32_t lastTickTimeCom = 0;
  uint32_t curTimeMicrosCom = micros();
  uint32_t elapsedTimeCom = curTimeMicrosCom - lastTickTimeCom;
  period_commutation_usPerTick = min(constrain(
      elapsedTimeCom,
      period_commutation_usPerTick - (PERIODSLEWLIM_US_PER_S*elapsedTimeCom/1e6),
      period_commutation_usPerTick + (PERIODSLEWLIM_US_PER_S*elapsedTimeCom/1e6)),
    10000);
  lastTickTimeCom = curTimeMicrosCom;
  // Serial.print("Commutating!\t");
  // Serial.print(phase);
  // Serial.print('\t');
  // Serial.println(duty);
  if (duty <= (.001*MODULO)){
    writeTrap(0, -1); // writing -1 floats all phases
  } else {
    writeTrap(duty, phase);
  }
  // if (recentWriteState != phase){
  //   Serial.print(hallOrder[getHalls()]);
  //   Serial.print('\t');
  //   Serial.println(phase);
  // }
  recentWriteState = phase;
  updatePhase_BEMFdelay(phase);
}