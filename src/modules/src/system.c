/*
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * system.c - Top level module implementation
 */
#define DEBUG_MODULE "SYS"

#include <stdbool.h>

/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "debug.h"
#include "version.h"
#include "config.h"
#include "param.h"
#include "log.h"
#include "ledseq.h"
#include "pm.h"

#include "config.h"
#include "system.h"
#include "platform.h"
#include "storage.h"
#include "configblock.h"
#include "worker.h"
#include "freeRTOSdebug.h"
#include "uart_syslink.h"
#include "uart1.h"
#include "uart2.h"
#include "comm.h"
#include "stabilizer.h"
#include "commander.h"
#include "console.h"
#include "usblink.h"
#include "mem.h"
#include "proximity.h"
#include "watchdog.h"
#include "queuemonitor.h"
#include "buzzer.h"
#include "sound.h"
#include "sysload.h"
#include "estimator_kalman.h"
#include "deck.h"
#include "extrx.h"
#include "app.h"
#include "static_mem.h"
#include "peer_localization.h"
#include "cfassert.h"

#ifndef START_DISARMED
#define ARM_INIT true
#else
#define ARM_INIT false
#endif

/* Private variable */
static bool selftestPassed;
static bool canFly;
static bool armed = ARM_INIT;
static bool forceArm;
static bool isInit;

STATIC_MEM_TASK_ALLOC(systemTask, SYSTEM_TASK_STACKSIZE);

/* System wide synchronisation */
xSemaphoreHandle canStartMutex;
static StaticSemaphore_t canStartMutexBuffer;

/* Private functions */
static void systemTask(void *arg);

/* Public functions */
void systemLaunch(void)
{
  STATIC_MEM_TASK_CREATE(systemTask, systemTask, SYSTEM_TASK_NAME, NULL, SYSTEM_TASK_PRI);
}

// This must be the first module to be initialized!
void systemInit(void)
{
  if(isInit)
    return;

  canStartMutex = xSemaphoreCreateMutexStatic(&canStartMutexBuffer);
  xSemaphoreTake(canStartMutex, portMAX_DELAY);

  //usblinkInit();
  //DEBUG_PRINT("usblink init\n");
  sysLoadInit();
  //DEBUG_PRINT("sysLoad init\n");
  /* Initialized here so that DEBUG_PRINT (buffered) can be used early */
  debugInit();
  //DEBUG_PRINT("debug init\n");
  crtpInit();
  //DEBUG_PRINT("crtp init\n");
  consoleInit();
  //DEBUG_PRINT("console init\n");

  DEBUG_PRINT("----------------------------\n");
  DEBUG_PRINT("%s is up and running!\n", platformConfigGetDeviceTypeName());

  if (V_PRODUCTION_RELEASE) {
    DEBUG_PRINT("Production release %s\n", V_STAG);
  } else {
    DEBUG_PRINT("Build %s:%s (%s) %s\n", V_SLOCAL_REVISION,
                V_SREVISION, V_STAG, (V_MODIFIED)?"MODIFIED":"CLEAN");
  }
  DEBUG_PRINT("I am 0x%08X%08X%08X and I have %dKB of flash!\n",
              *((int*)(MCU_ID_ADDRESS+8)), *((int*)(MCU_ID_ADDRESS+4)),
              *((int*)(MCU_ID_ADDRESS+0)), *((short*)(MCU_FLASH_SIZE_ADDRESS)));

  configblockInit();
  DEBUG_PRINT("config init\n");
  storageInit(); // Only semaphore
  //DEBUG_PRINT("storage init\n");
  workerInit(); // Seems to be only software
  //DEBUG_PRINT("worker init\n");
  adcInit(); // Implement as python peripheral?
  // adcInit writes some reset signals to registers and enables ADC2
  //DEBUG_PRINT("adc init\n");
  ledseqInit();
  //DEBUG_PRINT("ledseq init\n");
  pmInit();
  //DEBUG_PRINT("pm init\n");
  buzzerInit(); // Literally nothing
  //DEBUG_PRINT("buzzer init\n");
  peerLocalizationInit(); // Literally nothing
  //DEBUG_PRINT("peer init\n");

#ifdef APP_ENABLED
  appInit(); // NOT ENABLED BY DEFAULT
  //DEBUG_PRINT("app init\n");
#endif

  isInit = true;
}

bool systemTest()
{
  bool pass=isInit;

  pass &= ledseqTest();
  DEBUG_PRINT("ledseq %u\n",pass);
  pass &= pmTest();
  DEBUG_PRINT("pm %u\n",pass);
  pass &= workerTest();
  DEBUG_PRINT("worker %u\n",pass);
  pass &= buzzerTest();
  DEBUG_PRINT("buzzer %u\n",pass);
  return pass;
}

/* Private functions implementation */

void systemTask(void *arg)
{
  bool pass = true;

  ledInit();
  ledSet(CHG_LED, 1);

#ifdef DEBUG_QUEUE_MONITOR
  queueMonitorInit();
#endif

#ifdef ENABLE_UART1
  uart1Init(9600);
#endif
#ifdef ENABLE_UART2
  uart2Init(115200);
#endif

  //Init the high-levels modules
  systemInit();
  DEBUG_PRINT("Passed through systemInit()\n");
  commInit(); // Radio?
  //DEBUG_PRINT("comm init\n");
  commanderInit(); // ???
  //DEBUG_PRINT("commander init\n");

  StateEstimatorType estimator = anyEstimator;
  estimatorKalmanTaskInit(); // Software, functional but not usable without sensors
  DEBUG_PRINT("Kalman estimator init\n"),
  deckInit(); // 1-wire...?
  DEBUG_PRINT("deck init\n");
  estimator = deckGetRequiredEstimator();
  DEBUG_PRINT("Got estimator...\n");
  stabilizerInit(estimator);
  DEBUG_PRINT("stabilizer init\n");
  if (deckGetRequiredLowInterferenceRadioMode() && platformConfigPhysicalLayoutAntennasAreClose())
  {
    platformSetLowInterferenceRadioMode();
  }
  soundInit(); // Software
  //DEBUG_PRINT("sound init\n");
  memInit(); // Uses CRTP
  //DEBUG_PRINT("mem init\n");

#ifdef PROXIMITY_ENABLED
  proximityInit();
#endif
  DEBUG_PRINT("Time to run tests!\n");
  //Test the modules
  pass &= systemTest();
  DEBUG_PRINT("system %u\n",pass);
  pass &= configblockTest();
  DEBUG_PRINT("configblock %u\n",pass);
  pass &= storageTest();
  DEBUG_PRINT("storage %u\n",pass);
  pass &= commTest();
  DEBUG_PRINT("comm %u\n",pass);
  pass &= commanderTest();
  DEBUG_PRINT("commander %u\n",pass);
  pass &= stabilizerTest();
  DEBUG_PRINT("stabilizer %u\n",pass);
  pass &= estimatorKalmanTaskTest();
  DEBUG_PRINT("estimatorKalman %u\n",pass);
  pass &= deckTest();
  DEBUG_PRINT("deck %u\n",pass);
  pass &= soundTest();
  DEBUG_PRINT("sound %u\n",pass);
  pass &= memTest();
  DEBUG_PRINT("mem %u\n",pass);
  pass &= watchdogNormalStartTest();
  DEBUG_PRINT("watchdogNormalStart %u\n",pass);
  pass &= cfAssertNormalStartTest();
  DEBUG_PRINT("cfAssertNormalStart %u\n",pass);
  pass &= peerLocalizationTest();
  DEBUG_PRINT("peerLocalization %u\n",pass);
  DEBUG_PRINT("After all tests!\n");

  //Start the firmware
  if(pass)
  {
    selftestPassed = 1;
    systemStart();
    soundSetEffect(SND_STARTUP);
    ledseqRun(&seq_alive);
    ledseqRun(&seq_testPassed);
  }
  else
  {
    selftestPassed = 0;
    if (systemTest())
    {
      while(1)
      {
        ledseqRun(&seq_testFailed);
        vTaskDelay(M2T(2000));
        // System can be forced to start by setting the param to 1 from the cfclient
        if (selftestPassed)
        {
	        DEBUG_PRINT("Start forced.\n");
          systemStart();
          break;
        }
      }
    }
    else
    {
      ledInit();
      ledSet(SYS_LED, true);
    }
  }
  DEBUG_PRINT("Free heap: %d bytes\n", xPortGetFreeHeapSize());

  workerLoop();

  //Should never reach this point!
  while(1)
    vTaskDelay(portMAX_DELAY);
}


/* Global system variables */
void systemStart()
{
  xSemaphoreGive(canStartMutex);
#ifndef DEBUG
  watchdogInit();
#endif
}

void systemWaitStart(void)
{
  //This permits to guarantee that the system task is initialized before other
  //tasks waits for the start event.
  while(!isInit)
    vTaskDelay(2);

  xSemaphoreTake(canStartMutex, portMAX_DELAY);
  xSemaphoreGive(canStartMutex);
}

void systemSetCanFly(bool val)
{
  canFly = val;
}

bool systemCanFly(void)
{
  return canFly;
}

void systemSetArmed(bool val)
{
  armed = val;
}

bool systemIsArmed()
{

  return armed || forceArm;
}

void vApplicationIdleHook( void )
{
  static uint32_t tickOfLatestWatchdogReset = M2T(0);

  portTickType tickCount = xTaskGetTickCount();

  if (tickCount - tickOfLatestWatchdogReset > M2T(WATCHDOG_RESET_PERIOD_MS))
  {
    tickOfLatestWatchdogReset = tickCount;
    watchdogReset();
  }

  // Enter sleep mode. Does not work when debugging chip with SWD.
  // Currently saves about 20mA STM32F405 current consumption (~30%).
#ifndef DEBUG
  { __asm volatile ("wfi"); }
#endif
}

/*System parameters (mostly for test, should be removed from here) */
PARAM_GROUP_START(cpu)
PARAM_ADD(PARAM_UINT16 | PARAM_RONLY, flash, MCU_FLASH_SIZE_ADDRESS)
PARAM_ADD(PARAM_UINT32 | PARAM_RONLY, id0, MCU_ID_ADDRESS+0)
PARAM_ADD(PARAM_UINT32 | PARAM_RONLY, id1, MCU_ID_ADDRESS+4)
PARAM_ADD(PARAM_UINT32 | PARAM_RONLY, id2, MCU_ID_ADDRESS+8)
PARAM_GROUP_STOP(cpu)

PARAM_GROUP_START(system)
PARAM_ADD(PARAM_INT8 | PARAM_RONLY, selftestPassed, &selftestPassed)
PARAM_ADD(PARAM_INT8, forceArm, &forceArm)
PARAM_GROUP_STOP(sytem)

/* Loggable variables */
LOG_GROUP_START(sys)
LOG_ADD(LOG_INT8, canfly, &canFly)
LOG_ADD(LOG_INT8, armed, &armed)
LOG_GROUP_STOP(sys)
