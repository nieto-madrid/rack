/*
 * RACK - Robotics Application Construction Kit
 * Copyright (C) 2005-2006 University of Hannover
 *                         Institute for Systems Engineering - RTS
 *                         Professor Bernardo Wagner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Authors
 *      Matthias Hentschel <hentschel@rts.uni-hannover.de>
 *      Frauke W�bbold <wuebbold@rts.uni-hannover.de>
 */

#include "pilot_lab.h"

// pilot states
#define PILOT_STATE_IDLE            0
#define PILOT_STATE_NEW_DEST        1
#define PILOT_STATE_ROTATE 	       	2
#define PILOT_STATE_DRIVE_TO    	3
#define PILOT_STATE_WALL_FOLLOW		4
#define PILOT_STATE_ROTATE_AT_DEST  5

//
// data structures
//

// external module parameter
arg_table_t argTab[] = {

    { ARGOPT_OPT, "chassisSys", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "The system number of the chassis module, default 0", { 0 } },

    { ARGOPT_OPT, "chassisInst", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "The instance number of the chassis module, default 0", { 0 } },

    { ARGOPT_OPT, "positionSys", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "The system number of the position module, default 0", { 0 } },

    { ARGOPT_OPT, "positionInst", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "The instance number of the position module, default 0", { 0 } },

    { ARGOPT_OPT, "scan2dSys", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "The system number of the scan2d module, default 0", { 0 } },

    { ARGOPT_OPT, "scan2dInst", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "The instance number of the scan2d module, default 0", { 0 } },

    { ARGOPT_OPT, "speedMax", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "Maximum speed in mm/s, default 300 mm/s", { 300 } },

    { ARGOPT_OPT, "vMaxSide", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "maximum lateral vehicle speed (> 0 for youbot)", { 0 } },

    { ARGOPT_OPT, "omegaMax", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "Maximum angular velocity in deg/s, default 30 deg/s", { 30 } },

    { ARGOPT_OPT, "varDistance", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "Distance variance in mm, default 1000 mm", { 1000 } },

    { ARGOPT_OPT, "varRho", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "Angular variance in deg, default 15 deg/s", { 15 } },

    { ARGOPT_OPT, "distanceMin", ARGOPT_REQVAL, ARGOPT_VAL_INT,
      "Minimum distance to maintain to obstacles, default 300 mm", { 300 } },

    { 0, "", 0, 0, "", { 0 } } // last entry
};

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
 int  PilotLab::moduleOn(void)
{
    int             ret;

    // get dynamic module parameter
    speedMax     = getInt32Param("speedMax");
    vMaxSide     = abs(getInt32Param("vMaxSide"));
    omegaMax     = (float)getInt32Param("omegaMax") * M_PI / 180.0f;
    varDistance  = getInt32Param("varDistance");
    varRho       = (float)getInt32Param("varRho") * M_PI / 180.0f;
    distanceMin  = getInt32Param("distanceMin");

    GDOS_PRINT("varDistance %d varRho %f\n", varDistance, varRho);

    // turn on chassis module
    ret = chassis->on();
    if (ret)
    {
        GDOS_ERROR("Can't turn on Chassis(%d/%d), code = %d\n",
                   chassisSys, chassisInst, ret);
        return ret;
    }

    // get parameter data from chassis module
    ret = chassis->getParam(&chasParData, sizeof(chassis_param_data));
    if (ret)
    {
        GDOS_ERROR("Can't get parameter from Chassis(%d/%d), code = %d\n",
                   chassisSys, chassisInst, ret);
        return ret;
    }

    // turn on position module
    ret = position->on();
    if (ret)
    {
        GDOS_ERROR("Can't turn on Position(%d/%d), code = %d\n",
                   positionSys, positionInst, ret);
        return ret;
    }

    // turn on scan2d module
    ret = scan2d->on();
    if (ret)
    {
        GDOS_ERROR("Can't turn on Scan2d(%d/%d), code = %d\n",
                   scan2dSys, scan2dInst, ret);
        return ret;
    }

    // get continuous data from scan2d module
    scan2dDataMbx.clean();
    ret = scan2d->getContData(0, &scan2dDataMbx, &dataBufferPeriodTime);
    if (ret)
    {
        GDOS_ERROR("Can't get continuous data from Scan2d(%d/%d), code = %d\n",
                   scan2dSys, scan2dInst, ret);
        return ret;
    }

    // upper bound pilot speed to chassis parameter
    if (speedMax > chasParData.vxMax)
    {
        speedMax = chasParData.vxMax;
        GDOS_PRINT("speedMax %f m/s,\n", (float)speedMax / 1000.0f);
    }
    if (vMaxSide > chasParData.vyMax)
    {
        vMaxSide = chasParData.vyMax;
        GDOS_PRINT("vMaxSide %f m/s,\n", (float)vMaxSide / 1000.0f);
    }
    if (omegaMax > chasParData.omegaMax)
    {
        omegaMax = chasParData.omegaMax;
        GDOS_PRINT("omegaMax %f m/s,\n", omegaMax);
    }
    GDOS_PRINT("Pilot is waiting for a new destination...\n");

    // init global variables
    pilotState      = PILOT_STATE_IDLE;
    speed           = 0;
    sideSpeed       = 0;
    omega           = 0.0f;
    comfortDistance = 2 * distanceMin;

    return RackDataModule::moduleOn(); // has to be last command in moduleOn();
}


void PilotLab::moduleOff(void)
{
    RackDataModule::moduleOff();        // has to be first command in moduleOff();

    chassis->move(0, 0, 0.0f);
    scan2d->stopContData(&scan2dDataMbx);
}


// realtime context
int  PilotLab::moduleLoop(void)
{
    int          i;
    int          ret;
    int          radius = 0;
    float        curve  = 0.0f;
    double       angle_Diff_Dest;
    float        distance_Min_Scan;
	float        AngleDiff;
    float        deltaX, deltaY, diagonalVelocityFactor;
    float        deltaXTemp, deltaYTemp;
    int          signSpeed, signSideSpeed;
    RackMessage  msgInfo;
    scan_point   scanPointMin;
    pilot_data*  pilotData = NULL;

    // tolerances
    //tolerance_Angle_Dest = 0.175f; // 10�
    tolerance_Angle_Dest = 0.087f; // 5�

    // get continuous data from scan2d module
    ret = scan2dDataMbx.recvDataMsgTimed(rackTime.toNano(2 * dataBufferPeriodTime), &scan2dMsg.data,
                                         sizeof(scan2dMsg), &msgInfo);
    if (ret)
    {
        GDOS_ERROR("Can't read continuous data from Scan2d(%d/%d), code = %d\n",
                   scan2dSys, scan2dInst, ret);
        return ret;
    }

    // new scan2d data received
    if (msgInfo.getType() == MSG_DATA &&
        msgInfo.getSrc()  == scan2d->getDestAdr())
    {
        // message parsing
        Scan2dData::parse(&msgInfo);

        // get current position
        ret = position->getData(&positionData, sizeof(position_data), scan2dMsg.data.recordingTime);
        if (ret)
        {
            GDOS_ERROR("Can't get data from Position(%d/%d), code = %d\n",
                       positionSys, positionInst, ret);
            return ret;
        }

        // search for closest scan-point to the robot in scan2dMsg
        scanPointMin.x = 0;
        scanPointMin.y = 0;
        scanPointMin.z = scan2dMsg.data.maxRange;

        for (i = 0; i < scan2dMsg.data.pointNum; i++)
        {
            if ((scan2dMsg.data.point[i].type & SCAN_POINT_TYPE_INVALID) == 0)
            {
                if (scan2dMsg.data.point[i].z < scanPointMin.z)
                {
                    scanPointMin.x = scan2dMsg.data.point[i].x;
                    scanPointMin.y = scan2dMsg.data.point[i].y;
                    scanPointMin.z = scan2dMsg.data.point[i].z;
                }
            }
        }

        // state machine
        switch (pilotState)
        {
            // pilot is waiting for a new destination
            case PILOT_STATE_IDLE:
                speed = 0;
                sideSpeed = 0;
                omega = 0.0f;
                break;

            // new destination received
            case PILOT_STATE_NEW_DEST:
                speed = 0;
                // place your code here !!!!

                break;

            // add new states here !!!!
        }
        

        // limit robot speed by the distance to obstacles
        if (speed != 0)
        {
            curve  = omega / (float)speed;
            radius = curve2Radius(curve);
        }
        else
        {
            curve  = 0.0f;
            radius = 0;
        }
        speed = safeSpeed(speed, radius, NULL, &scan2dMsg.data, &chasParData);
        
        // move chassis with limited speed
        ret = chassis->move(speed, sideSpeed, omega);
        if (ret)
        {
            GDOS_ERROR("Can't send move command to chassis, code = %d\n", ret);
            return ret;
        }


        // get datapointer from rackdatabuffer
        pilotData = (pilot_data *)getDataBufferWorkSpace();

        pilotData->recordingTime    = scan2dMsg.data.recordingTime;
        memcpy(&(pilotData->pos), &positionData.pos, sizeof(pilotData->pos));
        memcpy(&(pilotData->dest), &pilotDest.pos, sizeof(pilotData->dest));
        pilotData->speed            = speed;
        pilotData->curve            = curve;
        pilotData->splineNum        = 0;

        if (pilotState == PILOT_STATE_IDLE)
        {
            pilotData->distanceToDest   = -1;
        }
        else
        {
            pilotData->distanceToDest   = 1;
        }

        putDataBufferWorkSpace(sizeof(pilot_data));
    }

    // invalid data received
    else
    {
        GDOS_ERROR("Received unexpected message from %x to %x, type %d on scan2dDataMbx\n",
                   msgInfo.getSrc(), msgInfo.getDest(), msgInfo.getType());

        if (msgInfo.getType() > 0)
        {
            scan2dDataMbx.sendMsgReply(MSG_ERROR, &msgInfo);
        }
        return -ECOMM;
    }

    return 0;
}

int  PilotLab::moduleCommand(RackMessage *msgInfo)
{
    pilot_dest_data     *pDest;

    switch(msgInfo->getType())
    {
        // new pilot destination received
        case MSG_PILOT_SET_DESTINATION:
            if (status == MODULE_STATE_ENABLED)
            {
                // message parsing
                pDest = PilotDestData::parse(msgInfo);

                // store pilot destination in global variable
                pilotDest.pos.x     = pDest->pos.x;
                pilotDest.pos.y     = pDest->pos.y;
                pilotDest.pos.rho   = pDest->pos.rho;
                pilotDest.speed     = pDest->speed;

                pilotState          = PILOT_STATE_NEW_DEST;
                GDOS_PRINT("Received new destination, x = %dmm, y = %dmm, rho = %adeg\n",
                           pilotDest.pos.x, pilotDest.pos.y, pilotDest.pos.rho);

                cmdMbx.sendMsgReply(MSG_OK, msgInfo);
            }
            else
            {
                cmdMbx.sendMsgReply(MSG_ERROR, msgInfo);
            }
            break;

        default:
            // not for me -> ask RackDataModule
            return RackDataModule::moduleCommand(msgInfo);
      }
      return 0;
}


float PilotLab::controlOmega(int speed, int dCurr, int dSet, float rhoCurr, float rhoSet)
{
    float   curve, a, b;

    // normalise the angles between: -pi < angle <= pi
    rhoCurr = normaliseAngleSym0(rhoCurr);
    rhoSet  = normaliseAngleSym0(rhoSet);

    // lateral controller
    a = chasParData.pilotParameterA * (float)(dCurr - dSet);

    if (a > 30.0 * M_PI/180.0)
    {
        a = 30.0 * M_PI/180.0;
    }
    if (a < -30.0 * M_PI/180.0)
    {
        a = -30.0 * M_PI/180.0;
    }

    // orientation controller
    b = chasParData.pilotParameterB * (a + (rhoSet - rhoCurr));

    // limit angle value
    if (b > chasParData.omegaMax)
    {
        b = chasParData.omegaMax;
    }
    if (b < -chasParData.omegaMax)
    {
        b = -chasParData.omegaMax;
    }

    // calculate curve value
    curve = b / (float)abs(speed);

    // limit curve value
    if (curve > 1.0f / (float)chasParData.minTurningRadius)
    {
        curve = 1.0f / (float)chasParData.minTurningRadius;
   }
    if (curve < -1.0f / (float)chasParData.minTurningRadius)
    {
        curve = -1.0f / (float)chasParData.minTurningRadius;
    }

    return curve * (float)speed;
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

// init_flags (for init and cleanup)
#define INIT_BIT_DATA_MODULE        0
#define INIT_BIT_MBX_WORK           1
#define INIT_BIT_MBX_SCAN2D         2
#define INIT_BIT_PROXY_CHASSIS      3
#define INIT_BIT_PROXY_POSITION     4
#define INIT_BIT_PROXY_SCAN2D       5

int  PilotLab::moduleInit(void)
{
    int ret;

    // call RackDataModule init function (first command in init)
    ret = RackDataModule::moduleInit();
    if (ret)
    {
        return ret;
    }
    initBits.setBit(INIT_BIT_DATA_MODULE);

    //
    // create mailboxes
    //

    // work mailbox
    ret = createMbx(&workMbx, 10, sizeof(chassis_param_data),
                    MBX_IN_KERNELSPACE | MBX_SLOT);
    if (ret)
    {
        goto init_error;
    }
    initBits.setBit(INIT_BIT_MBX_WORK);

    // scan2d mailbox
    ret = createMbx(&scan2dDataMbx, 2, sizeof(scan2d_data_msg),
                    MBX_IN_KERNELSPACE | MBX_SLOT);
    if (ret)
    {
        goto init_error;
    }
    initBits.setBit(INIT_BIT_MBX_SCAN2D);


    //
    // create Proxys
    //

    // chassis proxy
    chassis = new ChassisProxy(&workMbx, chassisSys, chassisInst);
    if (!chassis)
    {
        ret = -ENOMEM;
        goto init_error;
    }
    initBits.setBit(INIT_BIT_PROXY_CHASSIS);

    // position proxy
    position = new PositionProxy(&workMbx, positionSys, positionInst);
    if (!position)
    {
        ret = -ENOMEM;
        goto init_error;
    }
    initBits.setBit(INIT_BIT_PROXY_POSITION);

    // scan2d proxy
    scan2d = new Scan2dProxy(&workMbx, scan2dSys, scan2dInst);
    if (!scan2d)
    {
        ret = -ENOMEM;
        goto init_error;
    }
    initBits.setBit(INIT_BIT_PROXY_SCAN2D);

    return 0;

init_error:
    moduleCleanup();
    return ret;
}


void PilotLab::moduleCleanup(void)
{
    // call RackDataModule cleanup function
    if (initBits.testAndClearBit(INIT_BIT_DATA_MODULE))
    {
        RackDataModule::moduleCleanup();
    }

    //
    // free proxies
    //
    if (initBits.testAndClearBit(INIT_BIT_PROXY_SCAN2D))
    {
        delete scan2d;
    }

    if (initBits.testAndClearBit(INIT_BIT_PROXY_POSITION))
    {
        delete position;
    }

    if (initBits.testAndClearBit(INIT_BIT_PROXY_CHASSIS))
    {
        delete chassis;
    }

    //
    // delete mailboxes
    //
    if (initBits.testAndClearBit(INIT_BIT_MBX_SCAN2D))
    {
        destroyMbx(&scan2dDataMbx);
    }

    if (initBits.testAndClearBit(INIT_BIT_MBX_WORK))
    {
        destroyMbx(&workMbx);
    }
}


PilotLab::PilotLab()
      : RackDataModule( MODULE_CLASS_ID,
                    5000000000llu,    // 5s datatask error sleep time
                    16,               // command mailbox slots
                    48,               // command mailbox data size per slot
                    MBX_IN_KERNELSPACE | MBX_SLOT,  // command mailbox flags
                    5,                // max buffer entries
                    10)               // data buffer listener
{
    // get static module parameter
    chassisSys   = getIntArg("chassisSys", argTab);
    chassisInst  = getIntArg("chassisInst", argTab);
    positionSys  = getIntArg("positionSys", argTab);
    positionInst = getIntArg("positionInst", argTab);
    scan2dSys    = getIntArg("scan2dSys", argTab);
    scan2dInst   = getIntArg("scan2dInst", argTab);

    dataBufferMaxDataSize   = sizeof(pilot_data_msg);
}


int  main(int argc, char *argv[])
{
    int ret;

    // get args
    ret = RackModule::getArgs(argc, argv, argTab, "PilotLab");
    if (ret)
    {
        printf("Invalid arguments -> EXIT \n");
        return ret;
    }

    PilotLab *pInst;

    // create new PilotLab
    pInst = new PilotLab();
    if (!pInst)
    {
        printf("Can't create new PilotLab -> EXIT\n");
        return -ENOMEM;
    }

    // init
    ret = pInst->moduleInit();
    if (ret)
        goto exit_error;

    pInst->run();
    return 0;

exit_error:
    delete (pInst);
    return ret;

}
