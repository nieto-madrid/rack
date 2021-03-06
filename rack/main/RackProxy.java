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
 *      Joerg Langenberg <joerg.langenberg@gmx.net>
 *
 */
package rack.main;

import rack.main.tims.*;

public abstract class RackProxy
{
    // Define global message types for RackProxy and RackDataProxy
    
    // global commands (positive)
    public static final byte MSG_ON = 1;
    public static final byte MSG_OFF = 2;
    public static final byte MSG_GET_STATUS = 3;
    public static final byte MSG_GET_DATA = 4;
    public static final byte MSG_GET_CONT_DATA = 6;
    public static final byte MSG_STOP_CONT_DATA = 7;
    public static final byte MSG_GET_NEXT_DATA = 8;
    public static final byte MSG_GET_PARAM = 9;
    public static final byte MSG_SET_PARAM = 10;

    // global returns (negative)
    public static final byte MSG_OK = Tims.MSG_OK;
    public static final byte MSG_ERROR = Tims.MSG_ERROR;
    public static final byte MSG_TIMEOUT = Tims.MSG_TIMEOUT;
    public static final byte MSG_NOT_AVAILABLE = Tims.MSG_NOT_AVAILABLE;
    public static final byte MSG_ENABLED = -4;
    public static final byte MSG_DISABLED = -5;
    public static final byte MSG_DATA = -6;
    public static final byte MSG_CONT_DATA = -7;
    public static final byte MSG_PARAM = -9;

    public static final byte MSG_POS_OFFSET = 20;
    public static final byte MSG_NEG_OFFSET = -20;

    protected int            commandMbx;
    protected TimsMbx        replyMbx;

    public static int        DEFAULT_REPLY_TIMEOUT = 2000;
    protected int            replyTimeout          = DEFAULT_REPLY_TIMEOUT;

    protected byte           currentSequenceNo     = 0;
    
    public RackProxy(int commandMbx, TimsMbx replyMbx)
    {
        this.commandMbx = commandMbx;
        this.replyMbx = replyMbx;
    }

    public synchronized void on()
    {
        currentSequenceNo++;
        try
        {
            replyMbx.send0(RackProxy.MSG_ON, commandMbx,
                    (byte) 0, currentSequenceNo);
            TimsMsg reply;

            do
            {
                reply = replyMbx.receive(replyTimeout);
            }
            while (reply.seqNr != currentSequenceNo);

            // System.out.println(RackName.nameString(replyMbx) + ": " +
            // RackName.nameString(commandMbx) + ".on");
        }
        catch (TimsException e)
        {
            System.out.println(RackName.nameString(replyMbx.getName()) + ": "
                    + RackName.nameString(commandMbx) + ".on " + e);
        }
    }

    public synchronized void off()
    {
        currentSequenceNo++;
        try
        {
            replyMbx.send0(RackProxy.MSG_OFF, commandMbx,
                    (byte) 0, currentSequenceNo);
            TimsMsg reply;

            do
            {
                reply = replyMbx.receive(replyTimeout);
            }
            while (reply.seqNr != currentSequenceNo);

            // System.out.println(RackName.nameString(replyMbx) + ": " +
            // RackName.nameString(commandMbx) + ".off");
        }
        catch (TimsException e)
        {
            System.out.println(RackName.nameString(replyMbx.getName()) + ": "
                    + RackName.nameString(commandMbx) + ".off " + e);
        }
    }

    public synchronized int getStatus()
    {
        currentSequenceNo++;
        try
        {
            replyMbx.send0(RackProxy.MSG_GET_STATUS, commandMbx,
                           (byte) 0, currentSequenceNo);
            TimsMsg reply;

            do
            {
                reply = replyMbx.receive(replyTimeout);
            }
            while (reply.seqNr != currentSequenceNo);

            /*
             * switch(reply.type) { case RackProxy.MSG_ENABLED:
             * System.out.println(replyMbx.nameString() + ": " +
             * RackName.nameString(commandMbx) + ".getStatus ENABLED"); break;
             * case RackProxy.MSG_DISABLED:
             * System.out.println(replyMbx.nameString() + ": " +
             * RackName.nameString(commandMbx) + ".getStatus DISABLED"); break;
             * case RackProxy.MSG_ERROR:
             * System.out.println(replyMbx.nameString() + ": " +
             * RackName.nameString(commandMbx) + ".getStatus ERROR"); break;
             * case RackProxy.MSG_NOT_AVAILABLE:
             * System.out.println(replyMbx.nameString() + ": " +
             * RackName.nameString(commandMbx) + ".getStatus NOT_AVAILABLE");
             * break; default: System.out.println(replyMbx.nameString() + ": " +
             * RackName.nameString(commandMbx) + ".getStatus ERROR unknown type " +
             * reply.type); break; }
             */

            return (reply.type);
        }
        catch (TimsTimeoutException e)
        {
            // System.out.println(RackName.nameString(replyMbx) + ": " +
            // RackName.nameString(commandMbx) +
            // ".getStatus TIMEOUT");

            return (RackProxy.MSG_TIMEOUT);

        }
        catch (TimsException e)
        {
            System.out
                    .println(RackName.nameString(replyMbx.getName()) + ": "
                            + RackName.nameString(commandMbx)
                            + ".getStatus ERROR " + e);

            return (RackProxy.MSG_ERROR);
        }
    }

    public synchronized RackParamMsg getParameter()
    {
        currentSequenceNo++;

        try
        {
            replyMbx.send0(RackProxy.MSG_GET_PARAM, commandMbx,
                          (byte) 0, currentSequenceNo);

            TimsRawMsg reply;

            do
            {
                reply = replyMbx.receive(replyTimeout);
            }
            while (reply.seqNr != currentSequenceNo);

            if (reply.type == RackProxy.MSG_PARAM)
            {
                return new RackParamMsg(reply);
            }
            else
            {
                throw new TimsException("unexpected reply type (" + reply.type + ")");
            }
        }
        catch (TimsException e)
        {
            System.out.println(RackName.nameString(replyMbx.getName()) + ": "
                    + RackName.nameString(commandMbx) + ".getModuleParameter " + e);
            return null;
        }
    }

    public synchronized void setParameter(RackParamMsg paramMsg)
    {
        currentSequenceNo++;
        try
        {
            replyMbx.send(RackProxy.MSG_SET_PARAM, commandMbx,
                    (byte) 0, currentSequenceNo, paramMsg);
            TimsMsg reply;

            do
            {
                reply = replyMbx.receive(replyTimeout);
            }
            while (reply.seqNr != currentSequenceNo);

            // System.out.println(RackName.nameString(replyMbx) + ": " +
            // RackName.nameString(commandMbx) + ".setParameter");
        }
        catch (TimsException e)
        {
            System.out.println(RackName.nameString(replyMbx.getName()) + ": "
                    + RackName.nameString(commandMbx) + ".setParameter " + e);
        }
    }

    public void setReplyTimeout(int replyTimeout)
    {
        this.replyTimeout = replyTimeout;
    }
    
    /**
     * @return instanceId
     */
    public int getInstanceId()
    {
        return RackName.instanceId(commandMbx);
    }

    /**
     * @return classId
     */
    public int getClassId()
    {
        return RackName.classId(commandMbx);
    }

    public final int getCommandMbx()
    {
        return commandMbx;
    }
}
