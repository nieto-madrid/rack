/*
 * RACK - Robotics Application Construction Kit
 * Copyright (C) 2005-2010 University of Hannover
 *                         Institute for Systems Engineering - RTS
 *                         Professor Bernardo Wagner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Authors
 *      Oliver Wulf        <wulf@rts.uni-hannover.de>
 *      Matthias Hentschel <hentschel@rts.uni-hannover.de>
 */

#ifndef __DATALOG_REC_H__
#define __DATALOG_REC_H__

#include <main/rack_data_module.h>
#include <tools/datalog_proxy.h>

#include <drivers/camera_proxy.h>
#include <drivers/chassis_proxy.h>
#include <drivers/clock_proxy.h>
#include <drivers/compass_proxy.h>
#include <drivers/gps_proxy.h>
#include <drivers/gyro_proxy.h>
#include <drivers/io_proxy.h>
#include <drivers/ladar_proxy.h>
#include <drivers/servo_drive_proxy.h>
#include <drivers/vehicle_proxy.h>
#include <navigation/grid_map_proxy.h>
#include <navigation/mcl_proxy.h>
#include <navigation/odometry_proxy.h>
#include <navigation/path_proxy.h>
#include <navigation/pilot_proxy.h>
#include <navigation/position_proxy.h>
#include <perception/obj_recog_proxy.h>
#include <perception/scan2d_proxy.h>
#include <perception/scan3d_proxy.h>



#define MODULE_CLASS_ID             DATALOG

#define DATALOG_SMALL_MBX_SIZE_MAX            20*1024  //20KB

#if defined (__MSG_VELODYNE__) || defined (__MSG_KINECT__)
#define DATALOG_LARGE_MBX_SIZE_MAX        5*1024*1024  //5MB
#else // (__MSG_SCANDRIVE__)
#define DATALOG_LARGE_MBX_SIZE_MAX        1*1024*1024  //1MB
#endif

typedef struct {
    datalog_data         data;
    datalog_log_info     logInfo[DATALOG_LOGNUM_MAX];
} __attribute__((packed)) datalog_data_msg;



/**
 * Datalog Recording
 *
 * @ingroup modules_datalog
 */
class DatalogRec : public RackDataModule {
    private:
        int         enableBinaryIo;
        char       *logInfoFileName;

        void*       smallContDataPtr;
        void*       largeContDataPtr;

        RackMutex   datalogMtx;

        // additional mailboxes
        RackMailbox workMbx;
        RackMailbox smallContDataMbx;
        RackMailbox largeContDataMbx;

    protected:
        // -> realtime context
        int  moduleOn(void);
        void moduleOff(void);
        int  moduleLoop(void);
        int  moduleCommand(RackMessage *msgInfo);

        int  getStatus(uint32_t destMbxAdr, RackMailbox *replyMbx,
                       uint64_t reply_timeout_ns);

        int  moduleOn(uint32_t destMbxAdr, RackMailbox *replyMbx,
                      uint64_t reply_timeout_ns);

        int  getContData(uint32_t destMbxAdr, rack_time_t requestPeriodTime,
                         RackMailbox *dataMbx, RackMailbox *replyMbx,
                         rack_time_t *realPeriodTime, uint64_t reply_timeout_ns);

        int  stopContData(uint32_t destMbxAdr, RackMailbox *dataMbx,
                          RackMailbox *replyMbx, uint64_t reply_timeout_ns);

        int  logInfoCurrentModules(datalog_log_info *logInfoAll, int num,
                                   datalog_log_info *logInfoCurrent, RackMailbox *replyMbx,
                                   uint64_t reply_timeout_ns);

        int  loadLogInfoFile(char *fileName, datalog_data *data);
        int  parseLogInfo(FILE *file, datalog_data *data);

        // -> non realtime context
        void moduleCleanup(void);

    public:
        FILE*              fileptr[DATALOG_LOGNUM_MAX];
        datalog_data_msg   datalogInfoMsg;

        virtual void logInfoAllModules(datalog_data *data);
        virtual void logInfoSetDataSize(datalog_data *data);
        virtual int  initLogFile();
        virtual int  logData(RackMessage *msgInfo);

        // constructor und destructor
        DatalogRec();
        ~DatalogRec() {};

        // -> non realtime context
        int  moduleInit(void);
};

#endif // __DATALOG_REC_H__
