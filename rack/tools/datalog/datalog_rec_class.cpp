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
 #include "datalog_rec_class.h"

#include <main/argopts.h>

// init_flags
#define INIT_BIT_SMALL_CONT_DATA_BUFFER     0
#define INIT_BIT_LARGE_CONT_DATA_BUFFER     1
#define INIT_BIT_DATA_MODULE                2
#define INIT_BIT_MBX_WORK                   3
#define INIT_BIT_MBX_SMALL_CONT_DATA        4
#define INIT_BIT_MBX_LARGE_CONT_DATA        5
#define INIT_BIT_MTX_CREATED                6

/*******************************************************************************
 *   !!! REALTIME CONTEXT !!!
 *
 *   moduleOn,
 *   moduleOff,
 *   moduleLoop,
 *   moduleCommand,
 *
 *   own realtime user functions
 ******************************************************************************/
int  DatalogRec::moduleOn(void)
{
    int         i, ret;
    int         logEnable;
    int         moduleMbx;
    char        string[100];
    rack_time_t periodTime;
    rack_time_t realPeriodTime;
    rack_time_t datalogPeriodTime = RACK_TIME_MAX;

    GDOS_DBG_INFO("Turn on...\n");

    RackTask::disableRealtimeMode();

    if (datalogInfoMsg.data.logNum == 0)
    {
        GDOS_ERROR("Can't turn on, no modules to log\n");
        return -EINVAL;
    }

    for (i = 0; i < datalogInfoMsg.data.logNum; i++)
    {
        fileptr[i] = NULL;
    }

    smallContDataMbx.clean();
    largeContDataMbx.clean();

    for (i = 0; i < datalogInfoMsg.data.logNum; i++)
    {
        logEnable  = datalogInfoMsg.logInfo[i].logEnable;
        periodTime = datalogInfoMsg.logInfo[i].periodTime;
        moduleMbx  = datalogInfoMsg.logInfo[i].moduleMbx;

        datalogInfoMsg.logInfo[i].bytesLogged = 0;
        datalogInfoMsg.logInfo[i].setsLogged  = 0;

        if (logEnable > 0)
        {
            // concatenate filename
            strcpy(string, (char *)datalogInfoMsg.data.logPathName);
            strcat(string, (char *)datalogInfoMsg.logInfo[i].filename);

            // open log file
            if ((fileptr[i] = fopen(string, "w")) == NULL)
            {
                GDOS_ERROR("Can't open file %n...\n", moduleMbx);
                return -EIO;
            }

            // turn on module
            ret = moduleOn(moduleMbx, &workMbx, 5000000000ll);

            // request continuous data on smallContDataMbx
            if (datalogInfoMsg.logInfo[i].maxDataLen <= DATALOG_SMALL_MBX_SIZE_MAX)
            {
                ret = getContData(moduleMbx, periodTime, &smallContDataMbx, &workMbx,
                                  &realPeriodTime, 1000000000ll);
                if (ret)
                {
                    GDOS_ERROR("SMALL: Can't get continuous data from module %n, code= %d\n", moduleMbx, ret);
                    return ret;
                }
            }

            // request continuous data on largeContDataMbx
            else
            {
                ret = getContData(moduleMbx, periodTime, &largeContDataMbx, &workMbx,
                                  &realPeriodTime, 1000000000ll);
                if (ret)
                {
                    GDOS_ERROR("LARGE:Can't get continuous data from module %n, code= %d\n", moduleMbx, ret);
                    return ret;
                }
            }

            GDOS_DBG_INFO("%n: -log data\n", moduleMbx);

            // set shortest period time
            if (realPeriodTime < datalogPeriodTime)
            {
                datalogPeriodTime = realPeriodTime;
            }
        }
    }

    dataBufferPeriodTime = datalogPeriodTime;

    ret = initLogFile();
    if (ret < 0)
    {
        GDOS_ERROR("Can't init log files, code= %i\n", ret);
        return ret;
    }

    return RackDataModule::moduleOn();  // has to be last command in moduleOn();
}

void DatalogRec::moduleOff(void)
{
    int         i;
    int         logEnable;
    rack_time_t periodTime;
    int         moduleMbx;

    RackDataModule::moduleOff();        // has to be first command in moduleOff();

    GDOS_DBG_INFO("Turn off...\n");

    for (i = 0; i < datalogInfoMsg.data.logNum; i++)
    {
        logEnable   = datalogInfoMsg.logInfo[i].logEnable;
        periodTime  = datalogInfoMsg.logInfo[i].periodTime;
        moduleMbx   = datalogInfoMsg.logInfo[i].moduleMbx;

        if (logEnable > 0)
        {
            if (datalogInfoMsg.logInfo[i].maxDataLen <= DATALOG_SMALL_MBX_SIZE_MAX)
            {
                stopContData(moduleMbx, &smallContDataMbx, &workMbx, 1000000000ll);
            }
            else
            {
                stopContData(moduleMbx, &largeContDataMbx, &workMbx, 1000000000ll);
            }

            if (fileptr[i] != NULL)
            {
                fclose(fileptr[i]);
            }
        }
    }

    RackTask::enableRealtimeMode();
}

int  DatalogRec::moduleLoop(void)
{
    int             i, ret;
    RackMessage     msgInfo;
    datalog_data    *pDatalogData = NULL;


    // get continuous data on largeContDataMbx
    ret = largeContDataMbx.recvDataMsgIf(largeContDataPtr, DATALOG_LARGE_MBX_SIZE_MAX,
                                         &msgInfo);
    if (ret && ret != -EWOULDBLOCK) // error
    {
        GDOS_ERROR("Can't read data on largeContDataMbx, code = %d\n", ret);
/*        targetStatus = MODULE_TSTATE_OFF;
        return ret;*/
    }


    // get continuous data on smallContDataMbx of no data available on largeContDataMbx
    if (ret == -EWOULDBLOCK)
    {
        ret = smallContDataMbx.recvDataMsgTimed(200000000llu, smallContDataPtr,
                                               DATALOG_SMALL_MBX_SIZE_MAX, &msgInfo);

        if (ret)
        {
            if (ret == -ETIMEDOUT)
            {
                return 0;
            }
            else
            {
                GDOS_ERROR("Can't read data on smallContDataMbx, code = %d\n", ret);
/*                targetStatus = MODULE_TSTATE_OFF;
                return ret;*/
            }
        }
    }

    if (msgInfo.getType() != MSG_DATA)
    {
        GDOS_ERROR("No data package from %i type %i\n", msgInfo.getSrc(), msgInfo.getType());
/*        targetStatus = MODULE_TSTATE_OFF;
        return -EINVAL;*/
    }

    datalogMtx.lock(RACK_INFINITE);

    // get datapointer from rackdatabuffer
    pDatalogData = (datalog_data *)getDataBufferWorkSpace();

    // log data
    ret = logData(&msgInfo);
    if (ret)
    {
/*        datalogMtx.unlock();
        GDOS_ERROR("Error while logging data, code= %i\n", ret);
        targetStatus = MODULE_TSTATE_OFF;
        return ret;*/
    }

    // write new data package
    pDatalogData->recordingTime = rackTime.get();
    pDatalogData->logNum        = 0;

    for (i = 0; i < datalogInfoMsg.data.logNum; i++)
    {
        memcpy(&pDatalogData->logInfo[i], &datalogInfoMsg.logInfo[i],
               sizeof(datalogInfoMsg.logInfo[i]));
        (pDatalogData->logNum)++;
    }


    putDataBufferWorkSpace(sizeof(datalog_data)+
                           pDatalogData->logNum * sizeof(datalog_log_info));
    datalogMtx.unlock();

    return 0;
}

int  DatalogRec::moduleCommand(RackMessage *msgInfo)
{
    int           ret;
    datalog_data *setLogData;

    switch (msgInfo->getType())
    {
        case MSG_DATALOG_INIT_LOG:
            if (status == MODULE_STATE_DISABLED)
            {
                // use common logInfo
                if (strlen(logInfoFileName) == 0)
                {
                    logInfoAllModules(&datalogInfoMsg.data);
                }
                // use logInfo from file
                else
                {
                    loadLogInfoFile(logInfoFileName, &datalogInfoMsg.data);
                    GDOS_DBG_INFO("Loaded %d logInfos from file %s\n", 
                                  datalogInfoMsg.data.logNum, logInfoFileName);
                }
                logInfoSetDataSize(&datalogInfoMsg.data);
            }
            cmdMbx.sendMsgReply(MSG_OK, msgInfo);
            break;

        case MSG_DATALOG_GET_LOG_STATUS:
             datalogInfoMsg.data.logNum = logInfoCurrentModules(datalogInfoMsg.logInfo,
                                               datalogInfoMsg.data.logNum, datalogInfoMsg.logInfo,
                                               &workMbx, 1000000000ll);

            cmdMbx.sendDataMsgReply(MSG_DATALOG_LOG_STATUS, msgInfo, 1, &datalogInfoMsg,
                                    sizeof(datalogInfoMsg));
            break;

        case MSG_DATALOG_SET_LOG:
            setLogData = DatalogData::parse(msgInfo);

            if (datalogInfoMsg.data.logNum == setLogData->logNum)
            {
                memcpy(&datalogInfoMsg.data, setLogData, sizeof(datalogInfoMsg));
                cmdMbx.sendMsgReply(MSG_OK, msgInfo);
            }
            else
            {
                cmdMbx.sendMsgReply(MSG_ERROR, msgInfo);
            }
            break;

        case MSG_SET_PARAM:
            ret = RackDataModule::moduleCommand(msgInfo);

            GDOS_DBG_INFO("Module parameter changed\n");
            enableBinaryIo  = getInt32Param("binaryIo");
            logInfoFileName = getStringParam("logInfoFileName");
            return ret;

        default:
            // not for me -> ask RackDataModule
            return RackDataModule::moduleCommand(msgInfo);
    }
    return 0;
}

int DatalogRec::initLogFile()
{
    int i, ret = 0;

    for (i = 0; i < datalogInfoMsg.data.logNum; i++)
    {
        if (datalogInfoMsg.logInfo[i].logEnable)
        {
            switch (RackName::classId(datalogInfoMsg.logInfo[i].moduleMbx))
            {
                case CAMERA:
                    ret = fprintf(fileptr[i], "%% Camera(%i/%i)\n"
                                  "%% recordingTime width height depth mode"
                                  " colorFilterId cameraFileNum\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case CHASSIS:
                    ret = fprintf(fileptr[i], "%% Chassis(%i/%i)\n"
                                  "%% recordingTime deltaX deltaY deltaRho"
                                  " vx vy omega battery activePilot\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case CLOCK:
                    ret = fprintf(fileptr[i], "%% Clock(%i/%i)\n"
                                  "%% recordingTime hour second day month year"
                                  " utcTime dayOfWeek syncMode varT\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case COMPASS:
                    ret = fprintf(fileptr[i], "%% Compass(%i/%i)\n"
                                  "%% recordingTime orientation varOrientation\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case GPS:
                    ret = fprintf(fileptr[i], "%% Gps(%i/%i)\n"
                                  "%% recordingTime mode latitude longitude"
                                  " altitude heading speed satelliteNum utcTime pdop"
                                  " pos.x pos.y pos.z pos.phi pos.psi pos.rho"
                                  " var.x var.y var.z var.phi var.psi var.rho\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case GYRO:
                    ret = fprintf(fileptr[i], "%% Gyro(%i/%i)\n"
                                  "%% recordingTime roll pitch yaw"
                                  " aX aY aZ wRoll wPitch wYaw\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case IO:
                    if (enableBinaryIo)
                    {
                        ret = fprintf(fileptr[i], "%% Io(%i/%i)\n"
                                      "%% recordingTime valueNum ioFileNum\n",
                                      RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                      RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    }
                    else
                    {
                        ret = fprintf(fileptr[i], "%% Io(%i/%i)\n"
                                      "%% recordingTime valueNum value[0] \n",
                                      RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                      RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    }
                    break;

                case LADAR:
                    ret = fprintf(fileptr[i], "%% Ladar(%i/%i)\n"
                                  "%% recordingTime duration maxRange"
                                  " startAngle endAngle pointNum"
                                  " point[0].angle point[0].distance point[0].type"
                                  " point[0].intensity\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case ODOMETRY:
                    ret = fprintf(fileptr[i], "%% Odometry(%i/%i)\n"
                                  "%% recordingTime pos.x pos.y pos.z"
                                  " pos.phi pos.psi pos.rho\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case SERVO_DRIVE:
                    ret = fprintf(fileptr[i], "%% Servodrive(%i/%i)\n"
                                  "%% recordingTime position\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case VEHICLE:
                    ret = fprintf(fileptr[i], "%% Vehicle(%i/%i)\n"
                                  "%% recordingTime speed omega throttle brake clutch"
                                  " steering gear parkBrake vehicleProtect activeController\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case GRID_MAP:
                    ret = fprintf(fileptr[i], "%% Gridmap(%i/%i)\n"
                                  "%% recordingTime offsetX offsetY"
                                  " scale gridNumX gridNumY gridMapFileNum\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case MCL:
                    ret = fprintf(fileptr[i], "%% Mcl(%i/%i)\n"
                                  "%% recordingTime pos.x pos.y pos.z"
                                  " pos.phi pos.psi pos.rho pointNum"
                                  " point[0].x point[0].y point[0].z point[0].type\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case PATH:
                    ret = fprintf(fileptr[i], "%% Path(%i/%i)\n"
                                  "%% recordingTime splineNum"
                                  " spline[0].basepoint.x spline[0].basepoint.y"
                                  " spline[0].basepoint.speed spline[0].basepoint.maxRadius"
                                  " spline[0].basepoint.type spline[0].basepoint.request"
                                  " spline[0].basepoint.lbo spline[0].basepoint.id"
                                  " spline[0].basepoint.wayId"
                                  " spline[0].startPos.x spline[0].startPos.y"
                                  " spline[0].startPos.rho spline[0].endPos.x"
                                  " spline[0].endPos.y spline[0].endPos.rho"
                                  " spline[0].centerPos.x spline[0].centerPos.y"
                                  " spline[0].centerPos.rho spline[0].length"
                                  " spline[0].radius spline[0].vMax spline[0].vStart"
                                  " spline[0].vEnd spline[0].accMax spline[0].decMax"
                                  " spline[0].type spline[0].request spline[0].lbo\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case PILOT:
                    ret = fprintf(fileptr[i], "%% Pilot(%i/%i)\n"
                                  "%% recordingTime pos.x pos.y pos.z"
                                  " pos.phi pos.psi pos.rho dest.x dest.y dest.z"
                                  " dest.phi dest.psi dest.rho speed curve distanceToDest splineNum"
                                  " spline[0].basepoint.x spline[0].basepoint.y"
                                  " spline[0].basepoint.speed spline[0].basepoint.maxRadius"
                                  " spline[0].basepoint.type spline[0].basepoint.request"
                                  " spline[0].basepoint.lbo spline[0].basepoint.id"
                                  " spline[0].basepoint.wayId spline[0].basepoint.layer"
                                  " spline[0].basepoint.actionStart spline[0].basepoint.actionEnd"
                                  " spline[0].startPos.x spline[0].startPos.y"
                                  " spline[0].startPos.rho spline[0].endPos.x"
                                  " spline[0].endPos.y spline[0].endPos.rho"
                                  " spline[0].centerPos.x spline[0].centerPos.y"
                                  " spline[0].centerPos.rho spline[0].length"
                                  " spline[0].radius spline[0].vMax spline[0].vStart"
                                  " spline[0].vEnd spline[0].accMax spline[0].decMax"
                                  " spline[0].type spline[0].request spline[0].lbo\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case POSITION:
                    ret = fprintf(fileptr[i], "%% Position(%i/%i)\n"
                                  "%% recordingTime"
                                  " pos.x pos.y pos.z pos.phi pos.psi pos.rho"
                                  " var.x var.y var.z var.phi var.psi var.rho\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case OBJ_RECOG:
                    ret = fprintf(fileptr[i], "%% ObjRecog(%i/%i)\n"
                                  "%% recordingTime"
                                  " refPos.x refPos.y refPos.z refPos.phi refPos.psi refPos.rho"
                                  " objectNum"
                                  " object[0].objectId object[0].pos.x object[0].pos.y object[0].pos.z"
                                  " object[0].pos.phi object[0].pos.psi object[0].pos.rho "
                                  " object[0].vel.x object[0].vel.y object[0].vel.z"
                                  " object[0].vel.phi object[0].vel.psi object[0].vel.rho"
                                  " object[0].dim.x object[0].dim.y object[0].dim.z"
                                  " object[0].prob object[0].imageArea.x object[0].imageArea.y"
                                  " object[0].imageArea.width object[0].imageArea.height\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case SCAN2D:
                    ret = fprintf(fileptr[i], "%% Scan2d(%i/%i)\n"
                                  "%% recordingTime duration maxRange sectorNum sectorIndex"
                                  " refPos.x refPos.y refPos.z refPos.phi refPos.psi refPos.rho"
                                  " pointNum scan2dFileNum\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;

                case SCAN3D:
                    ret = fprintf(fileptr[i], "%% Scan3d(%i/%i)\n"
                                  "%% recordingTime duration maxRange"
                                  " scanNum scanPointNum scanMode scanHardware"
                                  " sectorNum sectorIndex"
                                  " refPos.x refPos.y refPos.z refPos.phi refPos.psi refPos.rho"
                                  " pointNum scan3dFileNum\n",
                                  RackName::systemId(datalogInfoMsg.logInfo[i].moduleMbx),
                                  RackName::instanceId(datalogInfoMsg.logInfo[i].moduleMbx));
                    break;
            }
        }
    }

    return ret;
}

int DatalogRec::logData(RackMessage *msgInfo)
{
    int              i, j, ret;
    int              bytes;
    int              bytesMax;
    char*            extFilenamePtr;
    char             extFilenameBuf[100];
    char             fileNumBuf[20];
    FILE*            extFileptr;

    camera_data      *cameraData;
    chassis_data     *chassisData;
    clock_data       *clockData;
    compass_data     *compassData;
    gps_data         *gpsData;
    gyro_data        *gyroData;
    io_data          *ioData;
    ladar_data       *ladarData;
    odometry_data    *odometryData;
    servo_drive_data *servoData;
    vehicle_data     *vehicleData;
    grid_map_data    *gridMapData;
    mcl_data         *mclData;
    path_data        *pathData;
    pilot_data       *pilotData;
    position_data    *positionData;
    obj_recog_data   *objRecogData;
    scan2d_data      *scan2dData;
    scan3d_data      *scan3dData;

    for (i = 0; i < datalogInfoMsg.data.logNum; i++)
    {
        if (datalogInfoMsg.logInfo[i].moduleMbx == msgInfo->getSrc())
        {
            switch (RackName::classId(msgInfo->getSrc()))
            {
                case CAMERA:
                    cameraData = CameraData::parse(msgInfo);

                    strcpy(extFilenameBuf, (char *)datalogInfoMsg.data.logPathName);
                    strcat(extFilenameBuf, (char *)datalogInfoMsg.logInfo[i].filename);
                    extFilenamePtr = strtok((char *)extFilenameBuf, ".");
                    sprintf(fileNumBuf, "_%i", datalogInfoMsg.logInfo[i].setsLogged + 1);
                    strncat(extFilenameBuf, fileNumBuf, strlen(fileNumBuf));

                    if (cameraData->mode == CAMERA_MODE_JPEG)
                    {
                        strcat(extFilenameBuf, ".jpg");
                    }
                    else
                    {
                        strcat(extFilenameBuf, ".raw");
                    }

                    bytes = fprintf(fileptr[i], "%u %i %i %i %i %i %i\n",
                        (unsigned int)cameraData->recordingTime,
                        cameraData->width,
                        cameraData->height,
                        cameraData->depth,
                        cameraData->mode,
                        cameraData->colorFilterId,
                        datalogInfoMsg.logInfo[i].setsLogged + 1);

                    if ((extFileptr = fopen(extFilenameBuf , "w")) == NULL)
                    {
                        GDOS_ERROR("Can't open file for Mbx %n...\n",
                                   datalogInfoMsg.logInfo[i].moduleMbx);
                        return -EIO;
                    }

                    bytesMax = cameraData->width * cameraData->height * cameraData->depth / 8;
                    bytes += fwrite(&cameraData->byteStream[0],
                                    sizeof(cameraData->byteStream[0]), bytesMax, extFileptr);

                    fclose(extFileptr);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case CHASSIS:
                    chassisData = ChassisData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %f %f %f %f %f %f %f %u\n",
                        (unsigned int)chassisData->recordingTime,
                        chassisData->deltaX,
                        chassisData->deltaY,
                        chassisData->deltaRho,
                        chassisData->vx,
                        chassisData->vy,
                        chassisData->omega,
                        chassisData->battery,
                        chassisData->activePilot);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

              case CLOCK:
                    clockData = ClockData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %i %i %i %i %i %li %i %i %i\n",
                        (unsigned int)clockData->recordingTime,
                        clockData->hour,
                        clockData->minute,
                        clockData->second,
                        clockData->day,
                        clockData->month,
                        clockData->year,
                        (long int)clockData->utcTime,
                        clockData->dayOfWeek,
                        clockData->syncMode,
                        clockData->varT);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

              case COMPASS:
                    compassData = CompassData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %f %f\n",
                        (unsigned int)compassData->recordingTime,
                        compassData->orientation,
                        compassData->varOrientation);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case GPS:
                    gpsData = GpsData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %.16f %.16f %i %f %i %i %li %f %i %i %i "
                        "%f %f %f %i %i %i %f %f %f\n",
                        (unsigned int)gpsData->recordingTime,
                        gpsData->mode,
                        gpsData->latitude,
                        gpsData->longitude,
                        gpsData->altitude,
                        gpsData->heading,
                        gpsData->speed,
                        gpsData->satelliteNum,
                        (long int)gpsData->utcTime,
                        gpsData->pdop,
                        gpsData->pos.x,
                        gpsData->pos.y,
                        gpsData->pos.z,
                        gpsData->pos.phi,
                        gpsData->pos.psi,
                        gpsData->pos.rho,
                        gpsData->var.x,
                        gpsData->var.y,
                        gpsData->var.z,
                        gpsData->var.phi,
                        gpsData->var.psi,
                        gpsData->var.rho);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case GYRO:
                    gyroData = GyroData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %f %f %f %f %f %f %f %f %f\n",
                        (unsigned int)gyroData->recordingTime,
                        gyroData->roll,
                        gyroData->pitch,
                        gyroData->yaw,
                        gyroData->aX,
                        gyroData->aY,
                        gyroData->aZ,
                        gyroData->wRoll,
                        gyroData->wPitch,
                        gyroData->wYaw);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case IO:
                    ioData = IoData::parse(msgInfo);

                    // binary output
                    if (enableBinaryIo)
                    {
                        bytes = fprintf(fileptr[i], "%u %d %i\n",
                                        (unsigned int)ioData->recordingTime,
                                        ioData->valueNum,
                                        datalogInfoMsg.logInfo[i].setsLogged + 1);

                        strcpy(extFilenameBuf, (char *)datalogInfoMsg.data.logPathName);
                        strcat(extFilenameBuf, (char *)datalogInfoMsg.logInfo[i].filename);
                        extFilenamePtr = strtok((char *)extFilenameBuf, ".");
                        sprintf(fileNumBuf, "_%i", datalogInfoMsg.logInfo[i].setsLogged + 1);
                        strncat(extFilenameBuf, fileNumBuf, strlen(fileNumBuf));
                        strcat(extFilenameBuf, ".raw");

                        if ((extFileptr = fopen(extFilenameBuf , "w")) == NULL)
                        {
                            GDOS_ERROR("Can't open file for Mbx %n...\n",
                                datalogInfoMsg.logInfo[i].moduleMbx);
                            return -EIO;
                        }

                        bytes += fwrite(ioData, 1, sizeof(io_data) + ioData->valueNum, extFileptr);
                        fclose(extFileptr);
                    }
                    else
                    {
                        // ascii output
                        bytes = fprintf(fileptr[i], "%u %d",
                            (unsigned int)ioData->recordingTime,
                            ioData->valueNum);

                        for (j = 0; j < ioData->valueNum; j++)
                        {
                            bytes += fprintf(fileptr[i], " %i",
                                     ioData->value[j]);
                        }

                        bytes += fprintf(fileptr[i], "\n");
                    }

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case LADAR:
                    ladarData = LadarData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %i %f %f %i",
                        (unsigned int)ladarData->recordingTime,
                        ladarData->duration,
                        ladarData->maxRange,
                        ladarData->startAngle,
                        ladarData->endAngle,
                        ladarData->pointNum);

                    for (j = 0; j < ladarData->pointNum; j++)
                    {
                        bytes += fprintf(fileptr[i], " %f %i %i %i",
                            ladarData->point[j].angle,
                            ladarData->point[j].distance,
                            ladarData->point[j].type,
                            ladarData->point[j].intensity);
                    }

                    bytes += fprintf(fileptr[i], "\n");

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case ODOMETRY:
                    odometryData = OdometryData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %i %i %f %f %f\n",
                        (unsigned int)odometryData->recordingTime,
                        odometryData->pos.x,
                        odometryData->pos.y,
                        odometryData->pos.z,
                        odometryData->pos.phi,
                        odometryData->pos.psi,
                        odometryData->pos.rho);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case SERVO_DRIVE:
                    servoData = ServoDriveData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %f\n",
                        (unsigned int)servoData->recordingTime,
                        servoData->position);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case VEHICLE:
                    vehicleData = VehicleData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %f %f %f %f %f %i %i %i %i %u\n",
                        (unsigned int)vehicleData->recordingTime,
                        vehicleData->speed,
                        vehicleData->omega,
                        vehicleData->throttle,
                        vehicleData->brake,
                        vehicleData->clutch,
                        vehicleData->steering,
                        vehicleData->gear,
                        vehicleData->engine,
                        vehicleData->parkBrake,
                        vehicleData->vehicleProtect,
                        vehicleData->activeController);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case GRID_MAP:
                    gridMapData = GridMapData::parse(msgInfo);

                    strcpy(extFilenameBuf, (char *)datalogInfoMsg.data.logPathName);
                    strcat(extFilenameBuf, (char *)datalogInfoMsg.logInfo[i].filename);
                    extFilenamePtr = strtok((char *)extFilenameBuf, ".");
                    sprintf(fileNumBuf, "_%i", datalogInfoMsg.logInfo[i].setsLogged + 1);
                    strncat(extFilenameBuf, fileNumBuf, strlen(fileNumBuf));
                    strcat(extFilenameBuf, ".raw");

                    bytes = fprintf(fileptr[i], "%u %i %i %i %i %i %i\n",
                        (unsigned int)gridMapData->recordingTime,
                        gridMapData->offsetX,
                        gridMapData->offsetY,
                        gridMapData->scale,
                        gridMapData->gridNumX,
                        gridMapData->gridNumY,
                        datalogInfoMsg.logInfo[i].setsLogged + 1);

                    if ((extFileptr = fopen(extFilenameBuf , "w")) == NULL)
                    {
                        GDOS_ERROR("Can't open file for Mbx %n...\n",
                                   datalogInfoMsg.logInfo[i].moduleMbx);
                        return -EIO;
                    }

                    bytesMax = gridMapData->gridNumX * gridMapData->gridNumY;
                    bytes += fwrite(&gridMapData->occupancy[0],
                                    sizeof(gridMapData->occupancy[0]), bytesMax, extFileptr);

                    fclose(extFileptr);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case MCL:
                    mclData = MCLData::parse(msgInfo);

                    bytes = fprintf(fileptr[i], "%u %i %i %i %f %f %f %i",
                        (unsigned int)mclData->recordingTime,
                        mclData->pos.x,
                        mclData->pos.y,
                        mclData->pos.z,
                        mclData->pos.phi,
                        mclData->pos.psi,
                        mclData->pos.rho,
                        mclData->pointNum);

                    for (j = 0; j < mclData->pointNum; j++)
                    {
                        bytes += fprintf(fileptr[i], " %i %i %i %i",
                            mclData->point[j].x,
                            mclData->point[j].y,
                            mclData->point[j].z,
                            mclData->point[j].type);
                    }

                    bytes += fprintf(fileptr[i], "\n");

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case PATH:
                    pathData = PathData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i",
                        (unsigned int)pathData->recordingTime,
                        pathData->splineNum);

                    for (j = 0; j < pathData->splineNum; j++)
                    {
                        bytes += fprintf(fileptr[i], " %i %i %i %i %i %i %i %i %i %i %i %f %i %i %f"
                                                     " %i %i %f %i %i %i %i %i %i %i %i %i %i",
                            pathData->spline[j].basepoint.x,
                            pathData->spline[j].basepoint.y,
                            pathData->spline[j].basepoint.speed,
                            pathData->spline[j].basepoint.maxRadius,
                            pathData->spline[j].basepoint.type,
                            pathData->spline[j].basepoint.request,
                            pathData->spline[j].basepoint.lbo,
                            pathData->spline[j].basepoint.id,
                            pathData->spline[j].basepoint.wayId,
                            pathData->spline[j].startPos.x,
                            pathData->spline[j].startPos.y,
                            pathData->spline[j].startPos.rho,
                            pathData->spline[j].endPos.x,
                            pathData->spline[j].endPos.y,
                            pathData->spline[j].endPos.rho,
                            pathData->spline[j].centerPos.x,
                            pathData->spline[j].centerPos.y,
                            pathData->spline[j].centerPos.rho,
                            pathData->spline[j].length,
                            pathData->spline[j].radius,
                            pathData->spline[j].vMax,
                            pathData->spline[j].vStart,
                            pathData->spline[j].vEnd,
                            pathData->spline[j].accMax,
                            pathData->spline[j].decMax,
                            pathData->spline[j].type,
                            pathData->spline[j].request,
                            pathData->spline[j].lbo);
                    }

                    bytes += fprintf(fileptr[i], "\n");

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case PILOT:
                    pilotData = PilotData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %i %i %f %f %f %i %i %i %f %f %f %i %f %i %i",
                        (unsigned int)pilotData->recordingTime,
                        pilotData->pos.x,
                        pilotData->pos.y,
                        pilotData->pos.z,
                        pilotData->pos.phi,
                        pilotData->pos.psi,
                        pilotData->pos.rho,
                        pilotData->dest.x,
                        pilotData->dest.y,
                        pilotData->dest.z,
                        pilotData->dest.phi,
                        pilotData->dest.psi,
                        pilotData->dest.rho,
                        pilotData->speed,
                        pilotData->curve,
                        pilotData->distanceToDest,
                        pilotData->splineNum);

                    for (j = 0; j < pilotData->splineNum; j++)
                    {
                        bytes += fprintf(fileptr[i], " %i %i %i %i %i %i %i %i %i %i %i %i"
                                                     " %i %i %f %i %i %f"
                                                     " %i %i %f %i %i %i %i %i %i %i %i %i %i",
                            pilotData->spline[j].basepoint.x,
                            pilotData->spline[j].basepoint.y,
                            pilotData->spline[j].basepoint.speed,
                            pilotData->spline[j].basepoint.maxRadius,
                            pilotData->spline[j].basepoint.type,
                            pilotData->spline[j].basepoint.request,
                            pilotData->spline[j].basepoint.lbo,
                            pilotData->spline[j].basepoint.id,
                            pilotData->spline[j].basepoint.wayId,
                            pilotData->spline[j].basepoint.layer,
                            pilotData->spline[j].basepoint.actionStart,
                            pilotData->spline[j].basepoint.actionEnd,
                            pilotData->spline[j].startPos.x,
                            pilotData->spline[j].startPos.y,
                            pilotData->spline[j].startPos.rho,
                            pilotData->spline[j].endPos.x,
                            pilotData->spline[j].endPos.y,
                            pilotData->spline[j].endPos.rho,
                            pilotData->spline[j].centerPos.x,
                            pilotData->spline[j].centerPos.y,
                            pilotData->spline[j].centerPos.rho,
                            pilotData->spline[j].length,
                            pilotData->spline[j].radius,
                            pilotData->spline[j].vMax,
                            pilotData->spline[j].vStart,
                            pilotData->spline[j].vEnd,
                            pilotData->spline[j].accMax,
                            pilotData->spline[j].decMax,
                            pilotData->spline[j].type,
                            pilotData->spline[j].request,
                            pilotData->spline[j].lbo);
                    }

                    bytes += fprintf(fileptr[i], "\n");

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case POSITION:
                    positionData = PositionData::parse(msgInfo);
                    bytes = fprintf(fileptr[i], "%u %i %i %i %f %f %f %i %i %i %f %f %f\n",
                        (unsigned int)positionData->recordingTime,
                        positionData->pos.x,
                        positionData->pos.y,
                        positionData->pos.z,
                        positionData->pos.phi,
                        positionData->pos.psi,
                        positionData->pos.rho,
                        positionData->var.x,
                        positionData->var.y,
                        positionData->var.z,
                        positionData->var.phi,
                        positionData->var.psi,
                        positionData->var.rho);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case OBJ_RECOG:
                    objRecogData = ObjRecogData::parse(msgInfo);

                    bytes = fprintf(fileptr[i], "%u %i %i %i %f %f %f %i %i %i %f %f %f%i",
                        (unsigned int)objRecogData->recordingTime,
                        objRecogData->refPos.x,
                        objRecogData->refPos.y,
                        objRecogData->refPos.z,
                        objRecogData->refPos.phi,
                        objRecogData->refPos.psi,
                        objRecogData->refPos.rho,
                        objRecogData->varRefPos.x,
                        objRecogData->varRefPos.y,
                        objRecogData->varRefPos.z,
                        objRecogData->varRefPos.phi,
                        objRecogData->varRefPos.psi,
                        objRecogData->varRefPos.rho,
                        objRecogData->objectNum);

                    for (j = 0; j < objRecogData->objectNum; j++)
                    {
                        bytes += fprintf(fileptr[i], " %i %i %i %i %i %f %f %f %i %i %i %f %f %f %i %i %i %f %f %f %i %i %i %f %f %f %i %i %i %f %i %i %i %i",
                            objRecogData->object[j].objectId,
                            objRecogData->object[j].type,
                            objRecogData->object[j].pos.x,
                            objRecogData->object[j].pos.y,
                            objRecogData->object[j].pos.z,
                            objRecogData->object[j].pos.phi,
                            objRecogData->object[j].pos.psi,
                            objRecogData->object[j].pos.rho,
                            objRecogData->object[j].varPos.x,
                            objRecogData->object[j].varPos.y,
                            objRecogData->object[j].varPos.z,
                            objRecogData->object[j].varPos.phi,
                            objRecogData->object[j].varPos.psi,
                            objRecogData->object[j].varPos.rho,
                            objRecogData->object[j].vel.x,
                            objRecogData->object[j].vel.y,
                            objRecogData->object[j].vel.z,
                            objRecogData->object[j].vel.phi,
                            objRecogData->object[j].vel.psi,
                            objRecogData->object[j].vel.rho,
                            objRecogData->object[j].varVel.x,
                            objRecogData->object[j].varVel.y,
                            objRecogData->object[j].varVel.z,
                            objRecogData->object[j].varVel.phi,
                            objRecogData->object[j].varVel.psi,
                            objRecogData->object[j].varVel.rho,
                            objRecogData->object[j].dim.x,
                            objRecogData->object[j].dim.y,
                            objRecogData->object[j].dim.z,
                            objRecogData->object[j].prob,
                            objRecogData->object[j].imageArea.x,
                            objRecogData->object[j].imageArea.y,
                            objRecogData->object[j].imageArea.width,
                            objRecogData->object[j].imageArea.height);
                    }

                    bytes += fprintf(fileptr[i], "\n");

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case SCAN2D:
                    scan2dData = Scan2dData::parse(msgInfo);

                    strcpy(extFilenameBuf, (char *)datalogInfoMsg.data.logPathName);
                    strcat(extFilenameBuf, (char *)datalogInfoMsg.logInfo[i].filename);
                    extFilenamePtr = strtok((char *)extFilenameBuf, ".");
                    sprintf(fileNumBuf, "_%i", datalogInfoMsg.logInfo[i].setsLogged + 1);
                    strncat(extFilenameBuf, fileNumBuf, strlen(fileNumBuf));
                    strcat(extFilenameBuf, ".2d");

                    bytes = fprintf(fileptr[i], "%u %u %i %i %i %i %i %i %f %f %f %i %i\n",
                        (unsigned int)scan2dData->recordingTime,
                        (unsigned int)scan2dData->duration,
                        scan2dData->maxRange,
                        scan2dData->sectorNum,
                        scan2dData->sectorIndex,
                        scan2dData->refPos.x,
                        scan2dData->refPos.y,
                        scan2dData->refPos.z,
                        scan2dData->refPos.phi,
                        scan2dData->refPos.psi,
                        scan2dData->refPos.rho,
                        scan2dData->pointNum,
                        datalogInfoMsg.logInfo[i].setsLogged + 1);

                    if ((extFileptr = fopen(extFilenameBuf , "w")) == NULL)
                    {
                        GDOS_ERROR("Can't open file for Mbx %n...\n",
                                   datalogInfoMsg.logInfo[i].moduleMbx);
                        return -EIO;
                    }

                    for (j = 0; j < scan2dData->pointNum; j++)
                    {
                        bytes += fprintf(extFileptr, "%i %i %i %i %i %i\n",
                            scan2dData->point[j].x,
                            scan2dData->point[j].y,
                            scan2dData->point[j].z,
                            scan2dData->point[j].type,
                            scan2dData->point[j].segment,
                            scan2dData->point[j].intensity);
                    }

                    fclose(extFileptr);

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                case SCAN3D:
                    scan3dData = Scan3dData::parse(msgInfo);

                    strcpy(extFilenameBuf, (char *)datalogInfoMsg.data.logPathName);
                    strcat(extFilenameBuf, (char *)datalogInfoMsg.logInfo[i].filename);
                    extFilenamePtr = strtok((char *)extFilenameBuf, ".");
                    sprintf(fileNumBuf, "_%i", datalogInfoMsg.logInfo[i].setsLogged + 1);
                    strncat(extFilenameBuf, fileNumBuf, strlen(fileNumBuf));

                    bytes = fprintf(fileptr[i], "%u %u %i %i %i %i %i %i %i %i %i %i %f %f %f %i %i\n",
                        (unsigned int)scan3dData->recordingTime,
                        (unsigned int)scan3dData->duration,
                        scan3dData->maxRange,
                        scan3dData->scanNum,
                        scan3dData->scanPointNum,
                        scan3dData->scanMode,
                        scan3dData->scanHardware,
                        scan3dData->sectorNum,
                        scan3dData->sectorIndex,
                        scan3dData->refPos.x,
                        scan3dData->refPos.y,
                        scan3dData->refPos.z,
                        scan3dData->refPos.phi,
                        scan3dData->refPos.psi,
                        scan3dData->refPos.rho,
                        scan3dData->pointNum,
                        datalogInfoMsg.logInfo[i].setsLogged + 1);

                    // ascii output
                    strcat(extFilenameBuf, ".3d");
                    if ((extFileptr = fopen(extFilenameBuf , "w")) == NULL)
                    {
                        GDOS_ERROR("Can't open file for Mbx %n...\n",
                                   datalogInfoMsg.logInfo[i].moduleMbx);
                        return -EIO;
                    }

                    for (j = 0; j < scan3dData->pointNum; j++)
                    {
                        bytes += fprintf(extFileptr, "%i %i %i %i %i %i\n",
                            scan3dData->point[j].x,
                            scan3dData->point[j].y,
                            scan3dData->point[j].z,
                            scan3dData->point[j].type,
                            scan3dData->point[j].segment,
                            scan3dData->point[j].intensity);
                    }
                    fclose(extFileptr);

                    // binary output
                    if (enableBinaryIo)
                    {
                        strcat(extFilenameBuf, ".raw");
                        if ((extFileptr = fopen(extFilenameBuf , "w")) == NULL)
                        {
                            GDOS_ERROR("Can't open file for Mbx %n...\n",
                                       datalogInfoMsg.logInfo[i].moduleMbx);
                            return -EIO;
                        }

                        bytes += fwrite(scan3dData, 1, sizeof(scan3d_data) +
                                        sizeof(scan_point)*scan3dData->pointNum, extFileptr);
                        fclose(extFileptr);
                    }

                    datalogInfoMsg.logInfo[i].bytesLogged += bytes;
                    datalogInfoMsg.logInfo[i].setsLogged  += 1;
                    break;

                default:
                    GDOS_PRINT("No write function for module class %n\n", msgInfo->getSrc());
                    return -EINVAL;
            }

            if (bytes < 0)
            {
                GDOS_ERROR("Can't write data package from %n to file, code = %i\n",
                           msgInfo->getSrc(), bytes);
                return bytes;
            }

            ret = fflush(fileptr[i]);
            if (ret < 0)
            {
                GDOS_ERROR("Can't flush file\n");
                return ret;
            }
        }
    }

    return 0;
}

int DatalogRec::getStatus(uint32_t destMbxAdr, RackMailbox *replyMbx, uint64_t reply_timeout_ns)
{
    RackMessage msgInfo;
    int ret;

    if (!replyMbx)
    {
        return -EINVAL;
    }

    ret = replyMbx->sendMsg(MSG_GET_STATUS, destMbxAdr, 0);
    if (ret)
    {
        GDOS_DBG_DETAIL("Proxy cmd to %n: Can't send command %d, code = %d\n",
                        destMbxAdr, MSG_GET_STATUS, ret);
        return ret;
    }
    GDOS_DBG_DETAIL("Proxy cmd to %n: command %d has been sent\n",
                        destMbxAdr, MSG_GET_STATUS);

    while (1)
    {   // waiting for reply (without data)
        ret = replyMbx->recvMsgTimed(reply_timeout_ns, &msgInfo);
        if (ret)
        {
            GDOS_DBG_DETAIL("Proxy cmd to %n: Can't receive reply of "
                         "command %d, code = %d\n",
                         destMbxAdr, MSG_GET_STATUS, ret);
            return ret;
        }

        if (msgInfo.getSrc() == destMbxAdr)
        {
            switch(msgInfo.getType())
            {
                case MSG_TIMEOUT:
                    GDOS_DBG_DETAIL("Proxy cmd %d to %n: Replied - timeout -\n",
                                    MSG_GET_STATUS, destMbxAdr);
                    return -ETIMEDOUT;

                case MSG_NOT_AVAILABLE:
                    GDOS_DBG_DETAIL("Proxy cmd %d to %n: Replied - not available \n",
                                    MSG_GET_STATUS, destMbxAdr);
                    return -ENODATA;

                case MSG_ENABLED:
                case MSG_DISABLED:
                case MSG_ERROR:
                    return msgInfo.getType();
            }
        }
    } // while-loop
    return -EINVAL;
}

int DatalogRec::moduleOn(uint32_t destMbxAdr, RackMailbox *replyMbx, uint64_t reply_timeout_ns)
{
    RackMessage msgInfo;
    int          ret;

    if (!replyMbx)
    {
        return -EINVAL;
    }

    ret = replyMbx->sendMsg(MSG_ON, destMbxAdr, 0);
    if (ret)
    {
        GDOS_WARNING("Proxy cmd to %n: Can't send command %d, code = %d\n",
                        destMbxAdr, MSG_ON, ret);
        return ret;
    }

    while (1)
    {
        // waiting for reply (without data)
        ret = replyMbx->recvMsgTimed(reply_timeout_ns, &msgInfo);
        if (ret)
        {
            GDOS_WARNING("Proxy cmd to %n: Can't receive reply of "
                         "command %d, code = %d\n",
                         destMbxAdr, MSG_ON, ret);
            return ret;
        }

        if (msgInfo.getSrc() == destMbxAdr)
        {
            switch(msgInfo.getType())
            {
                case MSG_ERROR:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - error -\n",
                                 MSG_ON, destMbxAdr);
                    return -ECOMM;

                case MSG_TIMEOUT:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - timeout -\n",
                                 MSG_ON, destMbxAdr);
                    return -ETIMEDOUT;

                case MSG_NOT_AVAILABLE:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - not available \n",
                                 MSG_ON, destMbxAdr);
                    return -ENODATA;

                case MSG_OK:
                    return 0;
            }
        }
    } // while-loop
    return -EINVAL;
}


int DatalogRec::getContData(uint32_t destMbxAdr, rack_time_t requestPeriodTime,
                             RackMailbox *dataMbx, RackMailbox *replyMbx,
                             rack_time_t *realPeriodTime, uint64_t reply_timeout_ns)
{
    int ret;
    RackMessage        msgInfo;
    rack_get_cont_data  send_data;
    rack_cont_data      recv_data;

    send_data.periodTime = requestPeriodTime;
    send_data.dataMbxAdr = dataMbx->getAdr();


    if (!replyMbx)
    {
        return -EINVAL;
    }

    ret = replyMbx->sendDataMsg(MSG_GET_CONT_DATA, destMbxAdr, 0, 1, &send_data,
                                sizeof(rack_get_cont_data));
    if (ret)
    {
        GDOS_WARNING("Proxy cmd to %n: Can't send command %d, code = %d\n",
                     destMbxAdr, MSG_GET_CONT_DATA, ret);
        return ret;
    }

    while (1)
    {
        ret = replyMbx->recvDataMsgTimed(reply_timeout_ns, &recv_data,
                                         sizeof(rack_cont_data), &msgInfo);
        if (ret)
        {
            GDOS_WARNING("Proxy cmd to %n: Can't receive reply of "
                         "command %d, code = %d\n",
                         destMbxAdr, MSG_GET_CONT_DATA, ret);
            return ret;
        }

        if (msgInfo.getSrc() == destMbxAdr)
        {
            switch(msgInfo.getType())
            {
                case MSG_ERROR:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - error -\n",
                                 MSG_GET_CONT_DATA, destMbxAdr);
                    return -ECOMM;

                case MSG_TIMEOUT:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - timeout -\n",
                                 MSG_GET_CONT_DATA, destMbxAdr);
                    return -ETIMEDOUT;

                case MSG_NOT_AVAILABLE:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - not available \n",
                                 MSG_GET_CONT_DATA, destMbxAdr);
                    return -ENODATA;
            }

            if (msgInfo.getType() == MSG_CONT_DATA)
            {

                RackContData::parse(&msgInfo);

                if (realPeriodTime)
                {
                    *realPeriodTime = recv_data.periodTime;
                }

                return 0;
            }
        }
    } // while-loop
    return -EINVAL;
}

int DatalogRec::stopContData(uint32_t destMbxAdr, RackMailbox *dataMbx, RackMailbox *replyMbx,
                              uint64_t reply_timeout_ns)
{
    rack_stop_cont_data     send_data;
    RackMessage             msgInfo;
    int                     ret;

    send_data.dataMbxAdr = dataMbx->getAdr();

    if (!replyMbx)
    {
        return -EINVAL;
    }

    ret = replyMbx->sendDataMsg(MSG_STOP_CONT_DATA, destMbxAdr, 0, 1, &send_data,
                                sizeof(rack_stop_cont_data));
    if (ret)
    {
        GDOS_WARNING("Proxy cmd to %n: Can't send command %d, code = %d\n",
                     destMbxAdr, MSG_STOP_CONT_DATA, ret);
        return ret;
    }

    while (1)
    {
        // waiting for reply (without data)
        ret = replyMbx->recvMsgTimed(reply_timeout_ns, &msgInfo);
        if (ret)
        {
            GDOS_WARNING("Proxy cmd to %n: Can't receive reply of "
                         "command %d, code = %d\n",
                         destMbxAdr, MSG_STOP_CONT_DATA, ret);
            return ret;
        }

        if (msgInfo.getSrc() == destMbxAdr)
        {
            switch(msgInfo.getType())
            {
                case MSG_ERROR:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - error -\n",
                                 MSG_STOP_CONT_DATA, destMbxAdr);
                    return -ECOMM;

                case MSG_TIMEOUT:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - timeout -\n",
                                 MSG_STOP_CONT_DATA, destMbxAdr);
                    return -ETIMEDOUT;

                case MSG_NOT_AVAILABLE:
                    GDOS_WARNING("Proxy cmd %d to %n: Replied - not available \n",
                                 MSG_STOP_CONT_DATA, destMbxAdr);
                    return -ENODATA;

                case MSG_OK:
                return 0;
            }
        }
    } // while-loop
    return -EINVAL;
}

void DatalogRec::logInfoAllModules(datalog_data *data)
{
    int num;

    for (num = 0; num < DATALOG_LOGNUM_MAX; num++)
    {
        data->logInfo[num].logEnable = 0;
        data->logInfo[num].periodTime = 0;
        bzero(data->logInfo[num].filename, 40);
    }

    data->logNum = 0;
    num = data->logNum;

    data->logInfo[num].moduleMbx  = RackName::create(systemId, CAMERA, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "camera_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CAMERA, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "camera_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CAMERA, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "camera_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CAMERA, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "camera_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CHASSIS, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "chassis_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CLOCK, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "clock_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CLOCK, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "clock_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, CLOCK, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "clock_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, COMPASS, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "compass_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GPS, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "gps_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GPS, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "gps_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GPS, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "gps_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GPS, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "gps_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GYRO, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "gyro_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GYRO, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "gyro_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, IO, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "io_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, IO, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "io_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, LADAR, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "ladar_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, LADAR, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "ladar_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, LADAR, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "ladar_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, LADAR, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "ladar_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SERVO_DRIVE, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "servodrive_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SERVO_DRIVE, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "servodrive_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SERVO_DRIVE, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "servodrive_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SERVO_DRIVE, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "servodrive_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SERVO_DRIVE, 4);
    snprintf((char *)data->logInfo[num].filename, 40, "servodrive_4.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, VEHICLE, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "vehicle_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, ODOMETRY, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "odometry_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, ODOMETRY, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "odometry_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, ODOMETRY, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "odometry_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GRID_MAP, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "gridmap_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, GRID_MAP, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "gridmap_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, MCL, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "mcl_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, MCL, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "mcl_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, MCL, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "mcl_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, MCL, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "mcl_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, MCL, 4);
    snprintf((char *)data->logInfo[num].filename, 40, "mcl_4.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, PATH, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "path_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, PATH, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "path_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, PILOT, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "pilot_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, PILOT, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "pilot_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, PILOT, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "pilot_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, POSITION, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "position_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, POSITION, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "position_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, POSITION, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "position_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, OBJ_RECOG, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "obj_recog_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, OBJ_RECOG, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "obj_recog_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, OBJ_RECOG, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "obj_recog_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, OBJ_RECOG, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "obj_recog_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, OBJ_RECOG, 4);
    snprintf((char *)data->logInfo[num].filename, 40, "obj_recog_4.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, OBJ_RECOG, 5);
    snprintf((char *)data->logInfo[num].filename, 40, "obj_recog_5.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_2.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 3);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_3.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 4);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_4.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 5);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_5.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 6);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_6.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 7);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_7.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN2D, 8);
    snprintf((char *)data->logInfo[num].filename, 40, "scan2d_8.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN3D, 0);
    snprintf((char *)data->logInfo[num].filename, 40, "scan3d_0.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN3D, 1);
    snprintf((char *)data->logInfo[num].filename, 40, "scan3d_1.dat");
    num++;

    data->logInfo[num].moduleMbx = RackName::create(systemId, SCAN3D, 2);
    snprintf((char *)data->logInfo[num].filename, 40, "scan3d_2.dat");
    num++;

    data->logNum = num;
}

int DatalogRec::loadLogInfoFile(char *fileName, datalog_data *data)
{
    FILE   *file;
    int     i;

    // init data struct
    for (i = 0; i < DATALOG_LOGNUM_MAX; i++)
    {
        data->logInfo[i].logEnable = 0;
        data->logInfo[i].periodTime = 0;
        bzero(data->logInfo[i].filename, 40);
    }
    data->logNum = 0;

    // open logInfo file
    RackTask::disableRealtimeMode();
    if ((file = fopen(fileName, "r")) == NULL)
    {
        printf("Can't open logInfo file \"%s\"\n", fileName);
        RackTask::enableRealtimeMode();
        return -EIO;
    }

    // read log info
    while (parseLogInfo(file, data) >= 0)
    {
        i++;
    }

    // close logInfo file
    fclose(file);
    RackTask::disableRealtimeMode();

    // check for valid log entries
    if (data->logNum == 0)
    {
        GDOS_ERROR("No valid logInfos found in file %s\n", fileName);
        return -EIO;
    }

    return 0;
}

int DatalogRec::parseLogInfo(FILE *file, datalog_data *data)
{
    char   lineBuffer[256];
    char   *segBuffer;
    char   *endPtr;
    int    state = 0;

    // read a line in file
    if (fgets(lineBuffer, 256, file) == NULL)
    {
        return -EFAULT;
    }

    // remove LINE-FEED
    endPtr = strchr(lineBuffer, 0x0A);
    if (endPtr != 0)
    {
        *endPtr = '\0';

        // return if lineBuffer is empty after removing LINE-FEED
        if (strlen(lineBuffer) == 0)
        {
            return -EFAULT;
        }
    }

    // read log info
    for (segBuffer = strtok(lineBuffer," "); segBuffer != NULL;
         segBuffer = strtok(NULL, " "))
    {
        switch(state)
        {
            // moduleMbx
            case 0:
                data->logInfo[data->logNum].moduleMbx = strtol(segBuffer, &endPtr, 16);
                if (*endPtr != 0)
                {
                    GDOS_ERROR("Wrong character in moduleMbx of logNum %d\n", data->logNum);
                    return -EIO;
                }
                break;

            // filename
            case 1:
                snprintf((char *)data->logInfo[data->logNum].filename, 40, segBuffer);
                break;

            default:
                break;
        }

        state++;
    }

    // check argument number
    if (state < 2)
    {
        GDOS_ERROR("Wrong number of arguments for logInfo[%d]\n", data->logNum);
        return -EIO;
    }

    if (data->logNum < (DATALOG_LOGNUM_MAX - 1))
    {
        data->logNum++;
    }

    return 0;
}

void DatalogRec::logInfoSetDataSize(datalog_data *data)
{
    int     i;

    for (i = 0; i < data->logNum; i++)
    {
        switch (RackName::classId(data->logInfo[i].moduleMbx))
        {
            case CAMERA:
                data->logInfo[i].maxDataLen = sizeof(camera_data) + CAMERA_MAX_BYTES;
                break;

            case CHASSIS:
                data->logInfo[i].maxDataLen = sizeof(chassis_data);
                break;

            case CLOCK:
                data->logInfo[i].maxDataLen = sizeof(clock_data);
                break;

            case COMPASS:
                data->logInfo[i].maxDataLen = sizeof(compass_data);
                break;

            case GPS:
                data->logInfo[i].maxDataLen = sizeof(gps_data);
                break;

            case GYRO:
                data->logInfo[i].maxDataLen = sizeof(gyro_data);
                break;

            case IO:
                data->logInfo[i].maxDataLen = sizeof(io_data) + IO_BYTE_NUM_MAX;
                break;

            case LADAR:
                data->logInfo[i].maxDataLen = sizeof(ladar_data) +
                                              sizeof(int32_t) * LADAR_DATA_MAX_POINT_NUM;
                break;

            case SERVO_DRIVE:
                data->logInfo[i].maxDataLen = sizeof(servo_drive_data);
                break;

            case VEHICLE:
                data->logInfo[i].maxDataLen = sizeof(vehicle_data);
                break;

            case ODOMETRY:
                data->logInfo[i].maxDataLen = sizeof(odometry_data);
                break;

            case GRID_MAP:
                data->logInfo[i].maxDataLen = sizeof(grid_map_data) + GRID_MAP_NUM_MAX;
                break;

            case MCL:
                data->logInfo[i].maxDataLen = sizeof(mcl_data) +
                                              sizeof(mcl_data_point) * MCL_DATA_POINT_MAX;
                break;

            case PATH:
                data->logInfo[i].maxDataLen = sizeof(path_data) +
                                              sizeof(polar_spline) * PATH_SPLINE_MAX;
                break;

            case PILOT:
                data->logInfo[i].maxDataLen = sizeof(pilot_data) +
                                              sizeof(polar_spline) * PILOT_DATA_SPLINE_MAX;
                break;

            case POSITION:
                data->logInfo[i].maxDataLen = sizeof(position_data);
                break;

            case OBJ_RECOG:
                data->logInfo[i].maxDataLen = sizeof(obj_recog_data) +
                                              sizeof(obj_recog_object) * OBJ_RECOG_OBJECT_MAX;
                break;

            case SCAN2D:
                data->logInfo[i].maxDataLen = sizeof(scan2d_data) +
                                              sizeof(scan_point) * SCAN2D_POINT_MAX ;
                break;

            case SCAN3D:
                data->logInfo[i].maxDataLen = sizeof(scan3d_data) +
                                              sizeof(scan_point) * SCAN3D_POINT_MAX;
                break;
        }
    }
}


int DatalogRec::logInfoCurrentModules(datalog_log_info *logInfoAll, int num,
                                       datalog_log_info *logInfoCurrent, RackMailbox *replyMbx,
                                       uint64_t reply_timeout_ns)
{
    int i, status;
    int currNum = 0;

    for (i = 0; i < num; i++)
    {
        status = getStatus(logInfoAll[i].moduleMbx, replyMbx, reply_timeout_ns);

        if ((status == MSG_ENABLED) || (status == MSG_DISABLED))
        {
            memcpy(&logInfoCurrent[currNum], &logInfoAll[i],
                   sizeof(datalog_log_info));
            currNum++;
        }
    }

    return currNum;
}

/*******************************************************************************
 *   !!! NON REALTIME CONTEXT !!!
 *
 *   moduleInit,
 *   moduleCleanup,
 *   Constructor,
 *   Destructor,
 *   main,
 *
 *   own non realtime user functions
 ******************************************************************************/
int DatalogRec::moduleInit(void)
{
    int ret;

    // call RackDataModule init function (first command in init)
    ret = RackDataModule::moduleInit();
    if (ret)
    {
        return ret;
    }
    initBits.setBit(INIT_BIT_DATA_MODULE);

    // get static module parameter
    enableBinaryIo  = getInt32Param("binaryIo");
    logInfoFileName = getStringParam("logInfoFileName");

    // allocate memory for smallContData buffer
    smallContDataPtr = malloc(DATALOG_SMALL_MBX_SIZE_MAX);
    if (smallContDataPtr == NULL)
    {
        GDOS_ERROR("Can't allocate smallContData buffer\n");
        return -ENOMEM;
    }
    initBits.setBit(INIT_BIT_SMALL_CONT_DATA_BUFFER);

    // allocate memory for largeContData buffer
    largeContDataPtr = malloc(DATALOG_LARGE_MBX_SIZE_MAX);
    if (largeContDataPtr == NULL)
    {
        GDOS_ERROR("Can't allocate largeContData buffer\n");
        return -ENOMEM;
    }
    initBits.setBit(INIT_BIT_LARGE_CONT_DATA_BUFFER);

    // work mailbox
    ret = createMbx(&workMbx, 1, 128, MBX_IN_KERNELSPACE | MBX_SLOT);
    if (ret)
    {
        goto init_error;
    }
    initBits.setBit(INIT_BIT_MBX_WORK);

    // continuous-data mailbox for small messages
    ret = createMbx(&smallContDataMbx, 500, DATALOG_SMALL_MBX_SIZE_MAX,
                    MBX_IN_USERSPACE | MBX_SLOT);
    if (ret)
    {
        goto init_error;
    }
    initBits.setBit(INIT_BIT_MBX_SMALL_CONT_DATA);

    // continuous-data mailbox for large messages
    ret = createMbx(&largeContDataMbx, 10, DATALOG_LARGE_MBX_SIZE_MAX,
                    MBX_IN_USERSPACE | MBX_SLOT);
    if (ret)
    {
        goto init_error;
    }
    initBits.setBit(INIT_BIT_MBX_LARGE_CONT_DATA);

    // create datalog mutex
    ret = datalogMtx.create();
    if (ret)
    {
        goto init_error;
    }
    initBits.setBit(INIT_BIT_MTX_CREATED);

    return 0;

init_error:
    // !!! call local cleanup function !!!
    DatalogRec::moduleCleanup();
    return ret;
}

void DatalogRec::moduleCleanup(void)
{
    // call RackDataModule cleanup function
    if (initBits.testAndClearBit(INIT_BIT_DATA_MODULE))
    {
        RackDataModule::moduleCleanup();
    }

    // destroy mutex
    if (initBits.testAndClearBit(INIT_BIT_MTX_CREATED))
    {
        datalogMtx.destroy();
    }

    // delete mailboxes
    if (initBits.testAndClearBit(INIT_BIT_MBX_WORK))
    {
        destroyMbx(&workMbx);
    }

    if (initBits.testAndClearBit(INIT_BIT_MBX_LARGE_CONT_DATA))
    {
        destroyMbx(&largeContDataMbx);
    }

    if (initBits.testAndClearBit(INIT_BIT_MBX_SMALL_CONT_DATA))
    {
        destroyMbx(&smallContDataMbx);
    }

    if (initBits.testAndClearBit(INIT_BIT_LARGE_CONT_DATA_BUFFER))
    {
        free(largeContDataPtr);
    }

    if (initBits.testAndClearBit(INIT_BIT_SMALL_CONT_DATA_BUFFER))
    {
        free(smallContDataPtr);
    }
}

DatalogRec::DatalogRec(void)
      : RackDataModule( MODULE_CLASS_ID,
                    5000000000llu,    // 5s datatask error sleep time
                    16,               // command mailbox slots
                    sizeof(datalog_data) + // command mailbox data size per slot
                    DATALOG_LOGNUM_MAX * sizeof(datalog_log_info),
                    MBX_IN_USERSPACE | MBX_SLOT,  // command mailbox flags
                    10,               // max buffer entries
                    10)               // data buffer listener
{
    dataBufferMaxDataSize   = sizeof(datalog_data_msg);
}
