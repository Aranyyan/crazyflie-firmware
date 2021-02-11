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
 * comm.c - High level communication module
 */

#include <stdbool.h>

#include "config.h"

#include "crtp.h"
#include "console.h"
#include "crtpservice.h"
#include "param.h"
#include "log.h"
#include "eskylink.h"
#include "uart_syslink.h"
#include "radiolink.h"
#include "usblink.h"
#include "platformservice.h"
#include "syslink.h"
#include "crtp_localization_service.h"

#include "debug.h"

static bool isInit;

void commInit(void)
{
  if (isInit)
    return;

  uartslkInit(); //drivers/src/uart_syslink.c
  radiolinkInit(); //hal/src/radiolink.c

  /* These functions are moved to be initialized early so
   * that DEBUG_PRINT can be used early */
  // crtpInit();
  // consoleInit();

  crtpSetLink(radiolinkGetLink());

  crtpserviceInit(); //modules/src/crtpservice.c
  platformserviceInit(); //modules/src/platformservice.c
  logInit();  //modules/src/log.c
  paramInit(); //modules/src/param.c
  locSrvInit(); //modules/src/crtp_localization_service.c

  //setup CRTP communication channel
  //TODO: check for USB first and prefer USB over radio
  //if (usbTest())
  //  crtpSetLink(usbGetLink);
  //else if(radiolinkTest())
  //  crtpSetLink(radiolinkGetLink());

  isInit = true;
  commTest();
}

bool commTest(void)
{
  bool pass=isInit;

  pass &= radiolinkTest();
  DEBUG_PRINT("Running commTest:\n%d\n",pass);
  pass &= crtpTest();
  DEBUG_PRINT("%d\n",pass);
  pass &= crtpserviceTest();
  DEBUG_PRINT("%d\n",pass);
  pass &= platformserviceTest();
  DEBUG_PRINT("%d\n",pass);
  pass &= consoleTest();
  DEBUG_PRINT("%d\n",pass);
  pass &= paramTest();
  DEBUG_PRINT("%d\n",pass);

  return pass;
}
