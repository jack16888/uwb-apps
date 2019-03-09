/**
 * Copyright (C) 2017-2018, Decawave Limited, All Rights Reserved
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_bsp.h"
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_hal.h>
#include <dw1000/dw1000_phy.h>
#include <dw1000/dw1000_mac.h>
#include <dw1000/dw1000_ftypes.h>
#include <tdma/tdma.h>
#if MYNEWT_VAL(CCP_ENABLED)
#include <ccp/ccp.h>
#endif
#if MYNEWT_VAL(NRNG_ENABLED)
#include <nrng/nrng.h>
#endif
#if MYNEWT_VAL(TIMESCALE)
#include <timescale/timescale.h> 
#endif
#if MYNEWT_VAL(WCS_ENABLED)
#include <wcs/wcs.h>
#endif
#if MYNEWT_VAL(SURVEY_ENABLED)
#include <survey/survey.h>
#endif

static void nrng_complete_cb(struct os_event *ev) {
    assert(ev != NULL);
    assert(ev->ev_arg != NULL);

    hal_gpio_toggle(LED_BLINK_PIN);
    dw1000_dev_instance_t * inst = (dw1000_dev_instance_t *)ev->ev_arg;
    dw1000_nrng_instance_t * nranges = inst->nrng;
    nrng_frame_t * frame = nranges->frames[(nranges->idx)%nranges->nframes];

#ifdef VERBOSE
    if (inst->status.start_rx_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\": \"start_rx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    if (inst->status.start_tx_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\":\"start_tx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    if (inst->status.rx_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\":\"rx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    if (inst->status.rx_timeout_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\":\"rx_timeout_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
#endif
    if (frame->code == DWT_DS_TWR_NRNG_FINAL || frame->code == DWT_DS_TWR_NRNG_EXT_FINAL){
        frame->code = DWT_DS_TWR_NRNG_END;
    }
}
/*! 
 * @fn complete_cb(dw1000_dev_instance_t * inst)
 *
 * @brief This callback is in the interrupt context and is uses to schedule an pdoa_complete event on the default event queue.  
 * Processing should be kept to a minimum giving the context. All algorithms should be deferred to a thread on an event queue. 
 * In this example all postprocessing is performed in the pdoa_ev_cb.
 * input parameters
 * @param inst - dw1000_dev_instance_t * 
 *
 * output parameters
 *
 * returns none 
 */
/* The timer callout */
static struct os_callout slot_callout;
static bool complete_cb(dw1000_dev_instance_t * inst, dw1000_mac_interface_t * cbs){
    if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    os_callout_init(&slot_callout, os_eventq_dflt_get(), nrng_complete_cb, inst);
    os_eventq_put(os_eventq_dflt_get(), &slot_callout.c_ev);
    return true;
}

/*! 
 * @fn slot_timer_cb(struct os_event * ev)
 *
 * @brief In this example this timer callback is used to start_rx.
 *
 * input parameters
 * @param inst - struct os_event *  
 *
 * output parameters
 *
 * returns none 
 */

    
static void 
slot_cb(struct os_event * ev){
    assert(ev);

    tdma_slot_t * slot = (tdma_slot_t *) ev->ev_arg;
    tdma_instance_t * tdma = slot->parent;
    dw1000_dev_instance_t * inst = tdma->parent;
    uint16_t idx = slot->idx;

    dw1000_set_delay_start(inst, tdma_rx_slot_start(inst, idx));
    uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_request_frame_t))
                        + inst->nrng->config.rx_timeout_delay;    
          
    dw1000_set_rx_timeout(inst, timeout + 0x1000);
    dw1000_nrng_listen(inst, DWT_BLOCKING);
}

int main(int argc, char **argv){
    int rc;

    sysinit();
    hal_gpio_init_out(LED_BLINK_PIN, 1);
    hal_gpio_init_out(LED_1, 1);
    hal_gpio_init_out(LED_3, 1);

    dw1000_dev_instance_t * inst = hal_dw1000_inst(0);
    inst->config.rxauto_enable = false;
    inst->config.dblbuffon_enabled = true;
    dw1000_set_dblrxbuff(inst, inst->config.dblbuffon_enabled);  

    dw1000_mac_interface_t cbs = (dw1000_mac_interface_t){
        .id =  DW1000_APP0,
        .complete_cb = complete_cb
    };

    dw1000_mac_append_interface(inst, &cbs);
    inst->slot_id = MYNEWT_VAL(SLOT_ID);
    uint32_t utime = os_cputime_ticks_to_usecs(os_cputime_get32());
    printf("{\"utime\": %lu,\"exec\": \"%s\"}\n",utime,__FILE__); 
    printf("{\"utime\": %lu,\"msg\": \"device_id = 0x%lX\"}\n",utime,inst->device_id);
    printf("{\"utime\": %lu,\"msg\": \"PANID = 0x%X\"}\n",utime,inst->PANID);
    printf("{\"utime\": %lu,\"msg\": \"DeviceID = 0x%X\"}\n",utime,inst->my_short_address);
    printf("{\"utime\": %lu,\"msg\": \"partID = 0x%lX\"}\n",utime,inst->partID);
    printf("{\"utime\": %lu,\"msg\": \"lotID = 0x%lX\"}\n",utime,inst->lotID);
    printf("{\"utime\": %lu,\"msg\": \"xtal_trim = 0x%X\"}\n",utime,inst->xtal_trim);  
    printf("{\"utime\": %lu,\"msg\": \"frame_duration = %d usec\"}\n",utime,dw1000_phy_frame_duration(&inst->attrib, sizeof(twr_frame_final_t))); 
    printf("{\"utime\": %lu,\"msg\": \"SHR_duration = %d usec\"}\n",utime,dw1000_phy_SHR_duration(&inst->attrib)); 
    printf("{\"utime\": %lu,\"msg\": \"holdoff = %d usec\"}\n",utime,(uint16_t)ceilf(dw1000_dwt_usecs_to_usecs(inst->rng->config.tx_holdoff_delay))); 
    
    inst->slot_id = MYNEWT_VAL(SLOT_ID);
#if MYNEWT_VAL(CCP_ENABLED)
    if(inst->slot_id ==1)
        dw1000_ccp_start(inst, CCP_ROLE_MASTER);
    else
        dw1000_ccp_start(inst, CCP_ROLE_SLAVE);
#endif

#if MYNEWT_VAL(SURVEY_ENABLED)
    tdma_assign_slot(inst->tdma, survey_slot_range_cb, MYNEWT_VAL(SURVEY_RANGE_SLOT), NULL);
    tdma_assign_slot(inst->tdma, survey_slot_broadcast_cb, MYNEWT_VAL(SURVEY_BROADCAST_SLOT), NULL);
    for (uint16_t i = 4; i < MYNEWT_VAL(TDMA_NSLOTS); i++)
#else
    for (uint16_t i = 1; i < MYNEWT_VAL(TDMA_NSLOTS); i++)
#endif
        tdma_assign_slot(inst->tdma, slot_cb, i, NULL);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    assert(0);
    return rc;
}

