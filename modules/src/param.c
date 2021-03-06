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
 * param.h - Crazy parameter system source file.
 */
#include "stm32f10x_conf.h"
#include <string.h>
#include <errno.h>

/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"

#include "config.h"
#include "crtp.h"
#include "param.h"
#include "crc.h"
#include "console.h"


#define TOC_CH 0
#define READ_CH 1
#define WRITE_CH 2

#define CMD_RESET 0
#define CMD_GET_NEXT 1
#define CMD_GET_CRC 2

#define CMD_GET_ITEM 0
#define CMD_GET_INFO 1

//Private functions
static void paramTask(void * prm);
void paramTOCProcess(int command);


//These are set by the Linker
extern struct param_s _param_start;
extern struct param_s _param_stop;

//The following two function SHALL NOT be called outside paramTask!
static void paramWriteProcess(int id, void*);
static void paramReadProcess(int id);
static int variableGetIndex(int id);

//Pointer to the parameters list and length of it
static struct param_s * params;
static int paramsLen;
static uint32_t paramsCrc;
static int paramsCount = 0;

static bool isInit = false;

void paramInit(void)
{
  int i;

  if(isInit)
    return;

  params = &_param_start;
  paramsLen = &_param_stop - &_param_start;
  paramsCrc = crcSlow(params, paramsLen);
  
  for (i=0; i<paramsLen; i++)
  {
    if(!(params[i].type & PARAM_GROUP)) 
      paramsCount++;
  }
  
  
  //Start the param task
	xTaskCreate(paramTask, (const signed char * const)"PARAM",
				    configMINIMAL_STACK_SIZE, NULL, /*priority*/1, NULL);
  
  //TODO: Handle stored parameters!
  
  isInit = true;
}

bool paramTest(void)
{
  return isInit;
}

CRTPPacket p;

void paramTask(void * prm)
{
	crtpInitTaskQueue(CRTP_PORT_PARAM);
	
	while(1) {
		crtpReceivePacketBlock(CRTP_PORT_PARAM, &p);
		
		if (p.crtp_channel==TOC_CH)
		  paramTOCProcess(p.crtp_data[0]);
	  else if (p.crtp_channel==READ_CH)
		  paramReadProcess(p.crtp_data[0]);
		else if (p.crtp_channel==WRITE_CH)
		  paramWriteProcess(p.crtp_data[0], &p.crtp_data[1]);
	}
}

void paramTOCProcess(int command)
{
  int ptr = 0;
  char * group = "";
  int n=0;
  
  switch (command)
  {
  case CMD_GET_INFO: //Get info packet about the param implementation
    ptr = 0;
    group = "";
    p.crtp_header=CRTP_HEADER(CRTP_PORT_PARAM, TOC_CH);
    p.size=6;
    p.crtp_data[0]=CMD_GET_INFO;
    p.crtp_data[1]=paramsCount;
    memcpy(&p.crtp_data[2], &paramsCrc, 4);
    crtpSendPacket(&p);
    break;
  case CMD_GET_ITEM:  //Get param variable
    for (ptr=0; ptr<paramsLen; ptr++) //Ptr points a group
    {
      if (params[ptr].type & PARAM_GROUP)
      {
        if (params[ptr].type & PARAM_START)
          group = params[ptr].name;
        else
          group = "";
      }
      else                          //Ptr points a variable
      {
        if (n==p.crtp_data[1])
          break;
        n++;
      }
    }
    
    if (ptr<paramsLen)
    {
      p.crtp_header=CRTP_HEADER(CRTP_PORT_PARAM, TOC_CH);
      p.crtp_data[0]=CMD_GET_ITEM;
      p.crtp_data[1]=n;
      p.crtp_data[2]=params[ptr].type;
      memcpy(p.crtp_data+3, group, strlen(group)+1);
      memcpy(p.crtp_data+3+strlen(group)+1, params[ptr].name, strlen(params[ptr].name)+1);
      p.size=3+2+strlen(group)+strlen(params[ptr].name);
      crtpSendPacket(&p);      
    } else {
      p.crtp_header=CRTP_HEADER(CRTP_PORT_PARAM, TOC_CH);
      p.crtp_data[0]=CMD_GET_ITEM;
      p.size=1;
      crtpSendPacket(&p);
    }
    break;
  }
}

static void paramWriteProcess(int ident, void* valptr)
{
  int id;

  id = variableGetIndex(ident);
  
  if (id<0) {
    p.crtp_data[0] = -1;
    p.crtp_data[1] = ident;
    p.crtp_data[2] = ENOENT;
    p.size = 3;
    
    crtpSendPacket(&p);
    return;
  }

	if (params[id].type & PARAM_RONLY)
		return;

  switch (params[id].type & PARAM_BYTES_MASK)
  {
 	case PARAM_1BYTE:
 		*(uint8_t*)params[id].address = *(uint8_t*)valptr;
 		break;
    case PARAM_2BYTES:
  	  *(uint16_t*)params[id].address = *(uint16_t*)valptr;
      break;
 	case PARAM_4BYTES:
      *(uint32_t*)params[id].address = *(uint32_t*)valptr;
      break;
 	case PARAM_8BYTES:
      *(uint64_t*)params[id].address = *(uint64_t*)valptr;
      break;
  }
  
  crtpSendPacket(&p);
}

static void paramReadProcess(int ident)
{
  int id;

  id = variableGetIndex(ident);
  
  if (id<0) {
    p.crtp_data[0] = -1;
    p.crtp_data[1] = ident;
    p.crtp_data[2] = ENOENT;
    p.size = 3;
    
    crtpSendPacket(&p);
    return;
  }

  switch (params[id].type & PARAM_BYTES_MASK)
  {
 	case PARAM_1BYTE:
   		memcpy(&p.crtp_data[1], params[id].address, sizeof(uint8_t));
   		p.size = 1+sizeof(uint8_t);
   		break;
 		break;
    case PARAM_2BYTES:
   		memcpy(&p.crtp_data[1], params[id].address, sizeof(uint16_t));
   		p.size = 1+sizeof(uint16_t);
   		break;
    case PARAM_4BYTES:
      memcpy(&p.crtp_data[1], params[id].address, sizeof(uint32_t));
   		p.size = 1+sizeof(uint32_t);
   		break;
 	  case PARAM_8BYTES:
      memcpy(&p.crtp_data[1], params[id].address, sizeof(uint64_t));
   		p.size = 1+sizeof(uint64_t);
   		break;
  }
  
  crtpSendPacket(&p);
}

static int variableGetIndex(int id)
{
  int i;
  int n=0;
  
  for (i=0; i<paramsLen; i++)
  {
    if(!(params[i].type & PARAM_GROUP)) 
    {
      if(n==id)
        break;
      n++;
    }
  }
  
  if (i>=paramsLen)
    return -1;
  
  return i;
}


