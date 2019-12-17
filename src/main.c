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

#define STATE_DISCOVERY 0
#define STATE_COORDINATOR 1
#define STATE_CLIENT 2

void rescheduleInterval(void);

void rescheduleThreshold(void);

void rescheduleTimeout(void);

bool is_addr_bigger(char *str1, char *str2);

int16_t calculateMovingAverage(int16_t oldAverage, int16_t currentValue);

void addClient(ipv6_addr_t *clientsList, ipv6_addr_t clientIP, int *clientsListCount);

void clearClients(ipv6_addr_t *clientsList, int *clientsListCount);

bool addrInList(ipv6_addr_t *clientsList, ipv6_addr_t clientIP, int *clientsListCount);

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
    bool leaderAlive = true;
    int msgCounter = 0;
    int state = 0;
    ipv6_addr_t highestAddr = {{0}};
    ipv6_addr_t clientsList[8] = {{{0}}};
    int clientsListCount = 0;
    int16_t average = 0;

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
            LOG_DEBUG("+ ELECT_INTERVAL_EVENT.\n");
            if (state == STATE_DISCOVERY)
            {
                puts("Current State: STATE_DISCOVERY");
                if (!otherIPIsHigher)
                {
                    puts("Broadcaste eigene IP, da keine höherwertigere IP gefunden");
                    if (broadcast_id(&thisAddr) < 0)
                    {
                        printf("%s: failed\n", __func__);
                    }
                }
                rescheduleInterval();
                clearClients(clientsList, &clientsListCount);
            }
            else if (state == STATE_COORDINATOR)
            {
                puts("Current State: STATE_COORDINATOR");
                printf("Broadcaste den Mittelwert: %i\n", average);
                if (broadcast_sensor(average) < 0)
                {
                    printf("%s: failed\n", __func__);
                }
                average = sensor_read();
                puts("Sammle Sensordaten");
                for (int i = 0; i < clientsListCount; i++)
                {
                    coap_get_sensor(clientsList[i]);
                }
                rescheduleInterval();
            }

            msgCounter = 0;

            puts("_________________________________________________________");

            break;

        case ELECT_BROADCAST_EVENT:
            LOG_DEBUG("+ ELECT_BROADCAST_EVENT, from [%s]\n", (char *)m.content.ptr);
            if (is_addr_bigger(thisAddrStr, (char *)m.content.ptr))
            {
                if (state == STATE_DISCOVERY)
                {
                    puts("Current State: STATE_DISCOVERY");
                    puts("höherwertigere IP gefunden.");
                    otherIPIsHigher = true;
                }
                else if (state == STATE_COORDINATOR)
                {
                    puts("Current State: STATE_COORDINATOR");
                    printf("Höherwertigere IP: %s gefunden\n", (char *)m.content.ptr);
                    puts("Führe Reset aus");
                    puts("<><><><><><>Bleibe in STATE_DISCOVERY<><><><><><>");
                    /* send initial `TICK` to start eventloop */
                    msg_send(&interval_event.msg, this_main_pid);
                    /* send initial `TICK` to start eventloop */
                    msg_send(&leader_threshold_event.msg, this_main_pid);
                    clearClients(clientsList, &clientsListCount);
                    otherIPIsHigher = false;
                    firstRound = true;
                    leaderAlive = true;
                    msgCounter = 0;
                    state = STATE_DISCOVERY;
                    memset(&highestAddr, 0, sizeof(ipv6_addr_t));
                    average = 0;
                }
                else if (state == STATE_CLIENT)
                {
                    puts("Current State: STATE_CLIENT");
                    puts("Coordinator wechsel");
                    puts("Führe Reset aus");
                    puts("<><><><><><>Bleibe in STATE_DISCOVERY<><><><><><>");
                    /* send initial `TICK` to start eventloop */
                    msg_send(&interval_event.msg, this_main_pid);
                    /* send initial `TICK` to start eventloop */
                    msg_send(&leader_threshold_event.msg, this_main_pid);
                    clearClients(clientsList, &clientsListCount);
                    otherIPIsHigher = false;
                    firstRound = true;
                    leaderAlive = true;
                    msgCounter = 0;
                    state = STATE_DISCOVERY;
                    memset(&highestAddr, 0, sizeof(ipv6_addr_t));
                    average = 0;
                }
            }
            else
            {
                //Broadcast my IP once, so the other node hears me
                if (broadcast_id(&thisAddr) < 0)
                {
                    printf("%s: failed\n", __func__);
                }
            }
            char highestAddrStr[40];
            ipv6_addr_to_str(highestAddrStr, &highestAddr, 40);
            if (is_addr_bigger(highestAddrStr, (char *)m.content.ptr))
            {
                ipv6_addr_from_str(&highestAddr, (char *)m.content.ptr);
                printf("neue höchste Addr %s\n", (char *)m.content.ptr);
            }
            msgCounter++;

            puts("_________________________________________________________");

            break;

        case ELECT_LEADER_ALIVE_EVENT:
            LOG_DEBUG("+ ELECT_LEADER_ALIVE_EVENT.\n");
            puts("Nachricht vom Coordinator erhalten");
            leaderAlive = true;

            puts("_________________________________________________________");

            break;

        case ELECT_LEADER_TIMEOUT_EVENT:
            LOG_DEBUG("+ ELECT_LEADER_TIMEOUT_EVENT.\n");
            if (leaderAlive)
            {
                puts("COORDINATOR ist aktiv");
                leaderAlive = false;
                rescheduleTimeout();
            }
            else
            {
                puts("COORDINATOR ist nicht aktiv");
                puts("Führe Reset aus");
                puts("<><><><><><>Bleibe in STATE_DISCOVERY<><><><><><>");
                /* send initial `TICK` to start eventloop */
                msg_send(&interval_event.msg, this_main_pid);
                /* send initial `TICK` to start eventloop */
                msg_send(&leader_threshold_event.msg, this_main_pid);
                clearClients(clientsList, &clientsListCount);
                otherIPIsHigher = false;
                firstRound = true;
                leaderAlive = true;
                msgCounter = 0;
                state = STATE_DISCOVERY;
                memset(&highestAddr, 0, sizeof(ipv6_addr_t));
                average = 0;
            }

            puts("_________________________________________________________");

            break;

        case ELECT_NODES_EVENT:
            LOG_DEBUG("+ ELECT_NODES_EVENT, from [%s].\n", (char *)m.content.ptr);
            puts("Clientanmeldung erhalten\n");
            ipv6_addr_t clientIP;
            ipv6_addr_from_str(&clientIP, (char *)m.content.ptr);
            addClient(clientsList, clientIP, &clientsListCount);
            printf("Anzahl der Clients in der Liste: %i\n", clientsListCount);

            puts("_________________________________________________________");

            break;

        case ELECT_SENSOR_EVENT:
            LOG_DEBUG("+ ELECT_SENSOR_EVENT, value=%s\n", (char *)m.content.ptr);
            int16_t value = (int16_t)strtol((char *)m.content.ptr, NULL, 10);
            average = calculateMovingAverage(average, value);

            puts("_________________________________________________________");

            break;

        case ELECT_LEADER_THRESHOLD_EVENT:
            LOG_DEBUG("+ ELECT_LEADER_THRESHOLD_EVENT.\n");
            if (firstRound)
            {
                rescheduleThreshold();
                firstRound = false;

                puts("_________________________________________________________");

                break;
            }
            printf("msgCounter ist %i\n", msgCounter);
            if (otherIPIsHigher && msgCounter < 2)
            {
                puts("<><><><><><>Wechsle in STATE_CLIENT<><><><><><>");
                state = STATE_CLIENT;

                char highestAddrStr[40];
                ipv6_addr_to_str(highestAddrStr, &highestAddr, 40);
                char thisAddrStr[40];
                ipv6_addr_to_str(thisAddrStr, &thisAddr, 40);

                if (coap_put_node(highestAddr, thisAddr) == 0)
                {
                    printf("Clientanmeldung: %s, an Coordinator: %s", thisAddrStr, highestAddrStr);
                    printf("Success\n");
                }

                msg_send(&leader_timeout_event.msg, this_main_pid);

                puts("_________________________________________________________");

                break;
            }
            else if (!otherIPIsHigher && msgCounter < 2)
            {
                puts("<><><><><><>Wechsle in STATE_COORDINATOR<><><><><><>");
                state = STATE_COORDINATOR;
                rescheduleInterval();

                puts("_________________________________________________________");

                break;
            }
            else
            {
                puts("<><><><><><>Bleibe in STATE_DISCOVERY<><><><><><>");
            }

            msgCounter = 0;

            rescheduleThreshold();

            puts("_________________________________________________________");

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

void rescheduleThreshold(void)
{
    // remove existing event
    evtimer_del(&evtimer, &leader_threshold_event.event);
    // reset event timer offset
    leader_threshold_event.event.offset = ELECT_LEADER_THRESHOLD;
    // (re)schedule event message
    evtimer_add_msg(&evtimer, &leader_threshold_event, this_main_pid);
}

void rescheduleTimeout(void)
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

int16_t calculateMovingAverage(int16_t oldAverage, int16_t currentValue)
{
    int16_t Xi = oldAverage;
    int16_t x = currentValue;

    int16_t result = (int16_t)((double)((((16.0 - 1.0) / 16.0) * (double)Xi) + ((1.0 / 16.0) * (double)x)));

    return result;
}

void addClient(ipv6_addr_t *clientsList, ipv6_addr_t clientIP, int *clientsListCount)
{
    if (!addrInList(clientsList, clientIP, clientsListCount))
    {
        clientsList[*clientsListCount] = clientIP;
        *clientsListCount = *clientsListCount + 1;
        puts("Client IP in Liste hinzugefügt");
        if (*clientsListCount < 0)
        {
            puts("This will never happen");
        }
    }
    else
    {
        puts("Client bereits in der Liste");
    }
}

void clearClients(ipv6_addr_t *clientsList, int *clientsListCount)
{
    memset(clientsList, 0, sizeof(ipv6_addr_t) * 8);
    *clientsListCount = 0;
}

bool addrInList(ipv6_addr_t *clientsList, ipv6_addr_t clientIP, int *clientsListCount)
{
    for (int i = 0; i < *clientsListCount; i++)
    {
        if (ipv6_addr_cmp(clientsList + i, &clientIP) == 0)
        {
            return true;
        }
    }
    return false;
}