/*
 * Copyright (c) 2017 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     vslab-riot
 * @{
 *
 * @file
 * @brief       Leader Election Application
 *
 * @author      Sebastian Meiling <s@mlng.net>
 *
 * @}
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "log.h"

#include "net/gcoap.h"
#include "kernel_types.h"
#include "random.h"

#include "msg.h"
#include "evtimer_msg.h"
#include "xtimer.h"

#include "elect.h"

void rescheduleInterval(void);

static msg_t _main_msg_queue[ELECT_NODES_NUM];
static kernel_pid_t this_main_pid;

/**
 * @name event time configuration
 * @{
 */
static evtimer_msg_t evtimer;
static evtimer_msg_event_t interval_event = {
    .event = {.offset = ELECT_MSG_INTERVAL},
    .msg = {.type = ELECT_INTERVAL_EVENT}};
static evtimer_msg_event_t leader_timeout_event = {
    .event = {.offset = ELECT_LEADER_TIMEOUT},
    .msg = {.type = ELECT_LEADER_TIMEOUT_EVENT}};
static evtimer_msg_event_t leader_threshold_event = {
    .event = {.offset = ELECT_LEADER_THRESHOLD},
    .msg = {.type = ELECT_LEADER_THRESHOLD_EVENT}};
/** @} */

/**
 * @brief   Initialise network, coap, and sensor functions
 *
 * @note    This function should be called first to init the system!
 */
int setup(void)
{
    LOG_DEBUG("%s: begin\n", __func__);
    /* avoid unused variable error */
    (void)interval_event;
    (void)leader_timeout_event;
    (void)leader_threshold_event;

    msg_init_queue(_main_msg_queue, ELECT_NODES_NUM);
    kernel_pid_t main_pid = thread_getpid();
    this_main_pid = main_pid;

    if (net_init(main_pid) != 0)
    {
        LOG_ERROR("init network interface!\n");
        return 2;
    }
    if (coap_init(main_pid) != 0)
    {
        LOG_ERROR("init CoAP!\n");
        return 3;
    }
    if (sensor_init() != 0)
    {
        LOG_ERROR("init sensor!\n");
        return 4;
    }
    if (listen_init(main_pid) != 0)
    {
        LOG_ERROR("init listen!\n");
        return 5;
    }
    LOG_DEBUG("%s: done\n", __func__);
    evtimer_init_msg(&evtimer);
    /* send initial `TICK` to start eventloop */
    msg_send(&interval_event.msg, main_pid);
    return 0;
}

int main(void)
{
    /* this should be first */
    if (setup() != 0)
    {
        return 1;
    }

    ipv6_addr_t thisAddr;
    get_node_ip_addr(&thisAddr);
    char thisAddrStr[IPV6_ADDR_MAX_STR_LEN];
    if (ipv6_addr_to_str(thisAddrStr, &thisAddr, sizeof(thisAddrStr)) == NULL)
    {
        LOG_ERROR("%s: failed to convert IP address!\n", __func__);
        return 1;
    }

    printf("My addr: %s\n", thisAddrStr); //This works, but the print on the device is lost. It still works!!!!
    while (true)
    {
        msg_t m;
        msg_receive(&m);
        switch (m.type)
        {
        case ELECT_INTERVAL_EVENT:
            LOG_DEBUG("+ interval event.\n");
            if (broadcast_id(&thisAddr) < 0)
            {
                printf("%s: failed\n", __func__);
            }
            rescheduleInterval();

            break;
        case ELECT_BROADCAST_EVENT:
            LOG_DEBUG("+ broadcast event, from [%s]\n", (char *)m.content.ptr);
            printf("otterAddr: %s\n",  (char *)m.content.ptr);
            /**
             * @todo implement
             */
            break;
        case ELECT_LEADER_ALIVE_EVENT:
            LOG_DEBUG("+ leader event.\n");
            /**
             * @todo implement
             */
            break;
        case ELECT_LEADER_TIMEOUT_EVENT:
            LOG_DEBUG("+ leader timeout event.\n");
            /**
             * @todo implement
             */
            break;
        case ELECT_NODES_EVENT:
            LOG_DEBUG("+ nodes event, from [%s].\n", (char *)m.content.ptr);
            /**
             * @todo implement
             */
            break;
        case ELECT_SENSOR_EVENT:
            LOG_DEBUG("+ sensor event, value=%s\n", (char *)m.content.ptr);
            /**
             * @todo implement
             */
            break;
        case ELECT_LEADER_THRESHOLD_EVENT:
            LOG_DEBUG("+ leader threshold event.\n");
            /**
             * @todo implement
             */
            break;
        default:
            LOG_WARNING("??? invalid event (%x) ???\n", m.type);
            break;
        }
        /* !!! DO NOT REMOVE !!! */
        if ((m.type != ELECT_INTERVAL_EVENT) &&
            (m.type != ELECT_LEADER_TIMEOUT_EVENT) &&
            (m.type != ELECT_LEADER_THRESHOLD_EVENT))
        {
            msg_reply(&m, &m);
        }
    }
    /* should never be reached */
    return 0;
}

void rescheduleInterval(void)
{
    // remove existing event
    evtimer_del(&evtimer, &interval_event.event);
    // reset event timer offset
    interval_event.event.offset = ELECT_MSG_INTERVAL;
    // (re)schedule event message
    evtimer_add_msg(&evtimer, &interval_event, this_main_pid);
}