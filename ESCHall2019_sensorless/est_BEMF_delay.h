#ifndef EST_BEMF_H
#define EST_BEMF_H

#include "ADC_2019sensorless.h"
#include "config.h"
volatile uint32_t prevTickTime_BEMFdelay;
volatile uint32_t period_bemfdelay_usPerTick = 0;
extern volatile uint32_t period_commutation_usPerTick;

// #include "IntervalTimer.h"
// IntervalTimer delayCommutateTimer;
volatile uint32_t delayCommutateTimer = 0;
volatile bool delayCommutateFinished = true;

volatile void BEMFcrossing_isr();
extern void commutate_isr(uint8_t phase, commutateMode_t caller);
void delayCommutate_isr();

void updatePhase_BEMFdelay(uint8_t drivePhase);
static const uint8_t floatPhases[6] = {2, 0, 1, 2, 0, 1};
static const uint8_t highPhases[6]  = {0, 2, 2, 1, 1, 0};
// static const uint8_t floatPhases[6] = {VS_C, VS_A, VS_B, VS_C, VS_A, VS_B};
// static const uint8_t highPhases[6] = {VS_A, VS_C, VS_C, VS_B, VS_B, VS_A};
static const bool isRisingEdges[6] = {true, false, true, false, true, false};

volatile uint8_t floatPhase, highPhase;
volatile bool isRisingEdge;

volatile uint8_t curPhase_BEMFdelay;
uint16_t cmpVal;

extern volatile bool dir;
extern volatile uint8_t curPhase_BEMFdelay;
volatile uint8_t triggerPhase_delay;

extern volatile int16_t phaseAdvance_Q10;

extern volatile uint32_t ADCreadTime;
extern volatile uint16_t vsx_cnts[3]; // vsA, vsB, vsC

#define ZCSTORETAPS 3
volatile uint32_t prevZCtimes_us[1<<ZCSTORETAPS];
volatile uint16_t prevZCtimesInd = 0;

void updateBEMFdelay(uint32_t curTimeMicros) {
	// if ((!delayCommutateFinished) && (curTimeMicros >= delayCommutateTimer)){
	//   delayCommutate_isr();
	// }
}

volatile void BEMFcrossing_isr() {
	// if (!delayCommutateFinished || (triggerPhase_delay == curPhase_BEMFdelay)) {
	// 	return;
	// }
	uint32_t curTickTime_us = ADCreadTime;

	// correction for discrete ADC sampling
	#ifdef useTRIGDELAYCOMPENSATION
		uint32_t triggerDelay_us = period_bemfdelay_usPerTick * abs((int16_t)(vsx_cnts[floatPhase] - (vsx_cnts[highPhase]>>1))) / vsx_cnts[highPhase];
		triggerDelay_us = constrain(triggerDelay_us, (uint32_t)0, (uint32_t)1000000/PWM_FREQ);
		curTickTime_us -= triggerDelay_us;
	#endif

	uint32_t elapsedTime_us = curTickTime_us - prevTickTime_BEMFdelay;
	if (elapsedTime_us < (0.9*period_bemfdelay_usPerTick)) {
		if ((elapsedTime_us > 500) && (period_bemfdelay_usPerTick>(period_commutation_usPerTick*1.2)))
			period_bemfdelay_usPerTick *= .9;
		return;
	}
	delayCommutateFinished = false;
	triggerPhase_delay = curPhase_BEMFdelay;

	period_bemfdelay_usPerTick = (curTickTime_us - prevZCtimes_us[prevZCtimesInd]) >> ZCSTORETAPS;
	prevZCtimes_us[prevZCtimesInd] = curTickTime_us;
	prevZCtimesInd = (prevZCtimesInd+1) % (1<<ZCSTORETAPS);
	// period_bemfdelay_usPerTick = min(constrain(
	// 		elapsedTime_us,
	// 		period_bemfdelay_usPerTick - (PERIODSLEWLIM_US_PER_S*elapsedTime_us >> 20), ///1e6),
	// 		period_bemfdelay_usPerTick + (PERIODSLEWLIM_US_PER_S*elapsedTime_us >> 20)), ///1e6)),
	// 	100000);
	prevTickTime_BEMFdelay = curTickTime_us;

	// if (delayCommutateFinished){
	// 	delayCommutateTimer = 0;
	// }
	delayCommutateTimer = curTickTime_us + (period_bemfdelay_usPerTick>>1) - (((int32_t)(phaseAdvance_Q10 * (period_bemfdelay_usPerTick>>1))) >> 10);
	// delayCommutateTimer.begin(delayCommutate_isr, period_bemfdelay_usPerTick/2);
}
void delayCommutate_isr() {
	// delayCommutateTimer.end();
	delayCommutateFinished = true;

	uint8_t pos_BEMF = dir ? (triggerPhase_delay+1)%6 : (triggerPhase_delay+5)%6;

	commutate_isr(pos_BEMF, MODE_SENSORLESS_DELAY);

	volatile static bool GPIO0on;
	// digitalWrite(0, triggerPhase_delay==0);
	digitalWrite(1, pos_BEMF==0);
	GPIO0on = !GPIO0on;
}

float getSpeed_erps() {
	return 6000000.0/period_bemfdelay_usPerTick;
}


// IntervalTimer postSwitchDelayTimer;
volatile uint32_t timeToUpdateCmp;
extern volatile uint32_t period_commutation_usPerTick;
void updatePhase_BEMFdelay(uint8_t drivePhase) {
	if (curPhase_BEMFdelay != drivePhase){
		curPhase_BEMFdelay = drivePhase;
		if (curPhase_BEMFdelay >= 6){
			return;
		}
		timeToUpdateCmp = micros() + 500; //min(period_commutation_usPerTick/5, 200);
		delayCommutateFinished = true;

		floatPhase = floatPhases[curPhase_BEMFdelay];
		highPhase = highPhases[curPhase_BEMFdelay];
		isRisingEdge = isRisingEdges[curPhase_BEMFdelay] ^ (!dir);
		// adc->disableCompare(ADC_0);
		// adc->startContinuous(floatPhases[curPhase_BEMFdelay], ADC_0);
		// adc->startContinuous(highPhases[curPhase_BEMFdelay], ADC_1);
		// bool suc = postSwitchDelayTimer.begin(updateCmp_ADC, min(period_commutation_usPerTick/10, 1000));
	}
}

extern float bemfV;
void BEMFdelay_update() {
	// if ((!delayCommutateFinished) && (micros() >= delayCommutateTimer)){
	// delayCommutate_isr();
	// }

	if ((!delayCommutateFinished) || (triggerPhase_delay == curPhase_BEMFdelay)) {
		if ((int32_t)(ADCreadTime - delayCommutateTimer) > 0){
			delayCommutate_isr();
		}
	} else {
		if ((int32_t)(ADCreadTime - timeToUpdateCmp) < 0){
			return;
		}
		if (!isRisingEdge ^ (vsx_cnts[floatPhase] > (vsx_cnts[highPhase]>>1))){
			BEMFcrossing_isr();
			
			// static volatile int16_t LEDon;
			// #define LED_DIV 1
			// digitalWriteFast(0, HIGH);
			// // LEDon = !LEDon;
			// LEDon ++;
			// LEDon %= LED_DIV*2;
		}
		else {
			// digitalWriteFast(0, LOW);
		}
	}
}

#endif