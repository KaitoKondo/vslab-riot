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

#define STATE_ENTDECKUNG 0
#define STATE_KOORDINATOR 1
#define STATE_KLIENT 2

void rescheduleInterval(void);
void reschedThreshold(void);
void reschedTimeout(void);
bool is_addr_bigger(char *str1, char *str2);

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
    /* send initial `TICK` to start eventloop */
    msg_send(&leader_threshold_event.msg, main_pid);
    return 0;
}

int main(void)
{
    bool otherIPIsHigher = false;
    bool firstRound = true;
    int msgCounter = 0;
    int state = 0;
    ipv6_addr_t highestAddr = {{0}};

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
            printf("%i", state);
            if (!otherIPIsHigher)
            {
                if (broadcast_id(&thisAddr) < 0)
                {
                    printf("%s: failed\n", __func__);
                }
            }
            msgCounter = 0;
            if (state == STATE_ENTDECKUNG)
            {
                rescheduleInterval();
            }
            break;
        case ELECT_BROADCAST_EVENT:
            LOG_DEBUG("+ broadcast event, from [%s]\n", (char *)m.content.ptr);
            if (is_addr_bigger(thisAddrStr, (char *)m.content.ptr))
            {
                puts("Es liegt nicht an dir, es liegt an mir, aber ich denke nicht das es zwischen uns klppt, ich habe jemand höheren gefunden.");
                otherIPIsHigher = true;
            }
            char highestAddrStr[40];
            ipv6_addr_to_str(highestAddrStr, &highestAddr, 40);
            if (is_addr_bigger(highestAddrStr, (char *)m.content.ptr))
            {
                ipv6_addr_from_str(&highestAddr, (char *)m.content.ptr);
                printf("neue höchste Addr %s\n", (char *)m.content.ptr);
            }
            msgCounter++;
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
            if (firstRound)
            {
                reschedThreshold();
                firstRound = false;
                break;
            }
            printf("msgCounter ist %i\n", msgCounter);
            if (otherIPIsHigher)
            {
                puts("Inf");
            }
            else
            {
                puts("Sup");
            }
            if (otherIPIsHigher && msgCounter < 2)
            {
                puts("Ich bin Client");
                state = STATE_KLIENT;
                if (coap_put_node(highestAddr, thisAddr) == 0)
                {
                    printf("Success\n");
                }
                break;
            }
            else if (!otherIPIsHigher && msgCounter < 2)
            {
                puts("Ich bin Coordinator");
                state = STATE_KOORDINATOR;
                break;
            }
            else
            {
                puts("Ich bin unbestimmt");
            }

            reschedThreshold();
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

void reschedThreshold(void)
{
    // remove existing event
    evtimer_del(&evtimer, &leader_threshold_event.event);
    // reset event timer offset
    leader_threshold_event.event.offset = ELECT_LEADER_THRESHOLD;
    // (re)schedule event message
    evtimer_add_msg(&evtimer, &leader_threshold_event, this_main_pid);
}

void reschedTimeout(void)
{
    // remove existing event
    evtimer_del(&evtimer, &leader_timeout_event.event);
    // reset event timer offset
    leader_timeout_event.event.offset = ELECT_LEADER_TIMEOUT;
    // (re)schedule event message
    evtimer_add_msg(&evtimer, &leader_timeout_event, this_main_pid);
}

bool is_addr_bigger(char *str1, char *str2)
{
    ipv6_addr_t addr1;
    ipv6_addr_t addr2;

    ipv6_addr_from_str(&addr1, str1);
    ipv6_addr_from_str(&addr2, str2);

    int cmp = ipv6_addr_cmp(&addr1, &addr2);

    if (cmp < 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}