/* USER CODE BEGIN Header */
/* @@@@ USERCODE FILE FreeRTOS application body */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */



#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include "lwip/etharp.h"
#include "lwip/api.h"
#include "lwip/raw.h"
#include "lwip/ip_addr.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/icmp.h"
#include <string.h>


#define IP_ADDRESS  LWIP_MAKEU32(192,168,0,207)
#define IP_NETMASK  LWIP_MAKEU32(255,255,255,0)
#define IP_GW       LWIP_MAKEU32(0,0,0,0)



/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for tskDefault */
osThreadId_t tskDefaultHandle;
const osThreadAttr_t tskDefault_attributes = {
  .name = "tskDefault",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
extern void  xcp_demo_init(void);



static u8_t ping_recv_callback(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);


/* USER CODE END FunctionPrototypes */

void tskFctDefault(void *argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of tskDefault */
  tskDefaultHandle = osThreadNew(tskFctDefault, NULL, &tskDefault_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_tskFctDefault */
/**
  * @brief  Function implementing the tskDefault thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_tskFctDefault */
void tskFctDefault(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN tskFctDefault */

  /* Eth Status */
  extern struct netif gnetif;
  printf("Eth status: netif=%d link=%d\r\n", netif_is_up(&gnetif), netif_is_link_up(&gnetif));

  /* Static IP setup */
  ip4_addr_t fb_ip, fb_mask, fb_gw;
  fb_ip.addr   = PP_HTONL(IP_ADDRESS);
  fb_mask.addr = PP_HTONL(IP_NETMASK);
  fb_gw.addr   = PP_HTONL(IP_GW);
  netif_set_addr(&gnetif, &fb_ip, &fb_mask, &fb_gw);
  printf("IP Address: %s:5000\r\n", ip4addr_ntoa(netif_ip4_addr(&gnetif)));

  /* Register ICMP ping listener */
  struct raw_pcb *ping_pcb = raw_new(IP_PROTO_ICMP);
  raw_recv(ping_pcb, ping_recv_callback, NULL);
  raw_bind(ping_pcb, IP_ADDR_ANY);
  printf("Ping listener active, LED3_Red toggles on ping\r\n");

  /* Initialize the XCP demo */
  xcp_demo_init();

  /* Infinite loop */
  for(;;) {
   
    // Blink Green LED
    HAL_GPIO_WritePin(LED1_Green_GPIO_Port, LED1_Green_Pin, GPIO_PIN_SET);
    osDelay(500);
    HAL_GPIO_WritePin(LED1_Green_GPIO_Port, LED1_Green_Pin, GPIO_PIN_RESET);
    osDelay(500);
   
  }

  /* USER CODE END tskFctDefault */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


/**
  * @brief  ICMP raw receive callback – called for every incoming ICMP packet.
  *         Returns 0 so LwIP still processes (and replies to) the ping normally.
  */
static u8_t ping_recv_callback(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  /* Check if it's an Echo Request (type 8) */
  if (p->len >= sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr))
  {
    struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
    uint16_t ip_hlen = IPH_HL(iphdr) * 4;
    struct icmp_echo_hdr *icmphdr = (struct icmp_echo_hdr *)((uint8_t *)p->payload + ip_hlen);

    if (ICMPH_TYPE(icmphdr) == ICMP_ECHO)  /* Echo Request */
    {
      //ping_count++;
      HAL_GPIO_TogglePin(LED3_Red_GPIO_Port, LED3_Red_Pin);
      printf("PING from %s\r\n", ip4addr_ntoa(ip_2_ip4(addr)));
    }
  }
  return 0;  /* 0 = don't consume, let LwIP send the reply */
}

/* USER CODE END Application */

