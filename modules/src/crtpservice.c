/**
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
 * crtpservice.c - Implements low level services for CRTP
 */
#include "stm32f10x_conf.h"
#include <stdbool.h>

/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"

#include "crtp.h"
#include "crtpservice.h"

static bool isInit=false;

typedef enum {
  linkEcho   = 0x00,
  linkSource = 0x01,
  linkSink   = 0x02,
} LinkNbr;

void crtpserviceHandler(CRTPPacket *p);

void crtpserviceInit(void)
{
  if (isInit)
    return;

  // Register a callback to service the Link port
  crtpRegisterPortCB(CRTP_PORT_LINK, crtpserviceHandler);
  
  isInit = true;
}

bool crtpserviceTest(void)
{
  return isInit;
}

void crtpserviceHandler(CRTPPacket *p)
{
  switch (p->crtp_channel)
  {
    case linkEcho:
      crtpSendPacket(p);
      break;
    case linkSource:
      p->size = CRTP_MAX_DATA_SIZE;
      crtpSendPacket(p);
      break;
    case linkSink:
      /* Ignore packet */
      break;
    default:
      break;
  } 
}

