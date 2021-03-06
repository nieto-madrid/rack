/*
 * RACK - Robotics Application Construction Kit
 * Copyright (C) 2005-2006 University of Hannover
 *                         Institute for Systems Engineering - RTS
 *                         Professor Bernardo Wagner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Authors
 *      Oliver Wulf <wulf@rts.uni-hannover.de>
 *
 */
#ifndef __JOYSTICK_PROXY_H__
#define __JOYSTICK_PROXY_H__

#include <main/rack_proxy.h>
#include <main/defines/position3d.h>

//######################################################################
//# Joystick Data (static size - MESSAGE)
//######################################################################

/**
 * joystick data structure
 */
typedef struct {
    rack_time_t recordingTime;              /**< [ms] global timestamp (has to be first element)*/
    position_3d position;                   /**< joystick position in percent
                                                 (-100 <= position <= 100) */
    int32_t     buttons;                    /**< binary button information bit0->button0, etc */
} __attribute__((packed)) joystick_data;

class JoystickData
{
    public:
        static void le_to_cpu(joystick_data *data)
        {
            data->recordingTime = __le32_to_cpu(data->recordingTime);
            Position3D::le_to_cpu(&data->position);
            data->buttons       = __le32_to_cpu(data->buttons);
        }

        static void be_to_cpu(joystick_data *data)
        {
            data->recordingTime = __be32_to_cpu(data->recordingTime);
            Position3D::be_to_cpu(&data->position);
            data->buttons       = __be32_to_cpu(data->buttons);
        }

        static joystick_data* parse(RackMessage *msgInfo)
        {
            if (!msgInfo->p_data)
                return NULL;

            joystick_data *p_data = (joystick_data *)msgInfo->p_data;

            if (msgInfo->isDataByteorderLe()) // data in little endian
            {
                le_to_cpu(p_data);
            }
            else // data in big endian
            {
                be_to_cpu(p_data);
            }
            msgInfo->setDataByteorder();
            return p_data;
        }
};

/**
 * Hardware abstraction for joysticks.
 *
 * @ingroup proxies_drivers
 */
class JoystickProxy : public RackDataProxy
{

      public:

        JoystickProxy(RackMailbox *workMbx, uint32_t sys_id, uint32_t instance)
                : RackDataProxy(workMbx, sys_id, JOYSTICK, instance)
        {
            setDataTimeout(1000000000llu);    // 1s
        };

        ~JoystickProxy()
        {
        };

//
// joystick data
//

        int getData(joystick_data *recv_data, ssize_t recv_datalen,
                    rack_time_t timeStamp)
        {
            return getData(recv_data, recv_datalen, timeStamp, dataTimeout);
        }

        int getData(joystick_data *recv_data, ssize_t recv_datalen,
                    rack_time_t timeStamp, uint64_t reply_timeout_ns);
};

#endif //_JOYSTICK_PROXY_H_
