/*
 * RACK - Robotics Application Construction Kit
 * Copyright (C) 2005-2010 University of Hannover
 *                         Institute for Systems Engineering - RTS
 *                         Professor Bernardo Wagner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Authors
 *      Marko Reimer <reimer@rts.uni-hannover.de>
 *
 */
package rack.main.defines;

import java.io.*;

import rack.main.tims.*;
import rack.main.defines.*;

/**
 * @author wulf
 *
 * To change the template for this generated type comment go to
 * Window&gt;Preferences&gt;Java&gt;Code Generation&gt;Code and Comments
 */
public class ObjRecogObject
{
    public static final int OBJ_RECOG_OBJECT_TYPE_UNCLASSIFIED  = 0;
    public static final int OBJ_RECOG_OBJECT_TYPE_UNKNOWN_SMALL = 1;
    public static final int OBJ_RECOG_OBJECT_TYPE_UNKNOWN_BIG   = 2;
    public static final int OBJ_RECOG_OBJECT_TYPE_PEDESTRIAN    = 3;
    public static final int OBJ_RECOG_OBJECT_TYPE_BIKE          = 4;
    public static final int OBJ_RECOG_OBJECT_TYPE_CAR           = 5;
    public static final int OBJ_RECOG_OBJECT_TYPE_TRUCK         = 6;

    public int objectId = 0;
    public int type = 0;
    public Position3d pos = new Position3d();
    public Position3d varPos = new Position3d();
    public Position3d vel = new Position3d();
    public Position3d varVel = new Position3d();
    public Point3d    dim = new Point3d();
    public float      prob = 0.0f;
    public ImageRect  imageArea = new ImageRect();

    public static int getDataLen()
    {
        return (4 + 4 + 4 + 4 * Position3d.getDataLen() + ImageRect.getDataLen() + Point3d.getDataLen());
    }

    public ObjRecogObject()
    {
    }

    /**
     * @param dataIn
     */
    public ObjRecogObject(EndianDataInputStream dataIn)
            throws IOException
    {
        objectId = dataIn.readInt();
        type = dataIn.readInt();
        pos.readData(dataIn);
        varPos.readData(dataIn);
        vel.readData(dataIn);
        varVel.readData(dataIn);
        dim.readData(dataIn);
        prob = dataIn.readFloat();
        imageArea.readData(dataIn);
    }

    /**
     * @param dataOut
     */
    public void writeDataOut(DataOutputStream dataOut) throws IOException
    {
        dataOut.writeInt(objectId);
        dataOut.writeInt(type);
        pos.writeData(dataOut);
        varPos.writeData(dataOut);
        vel.writeData(dataOut);
        varVel.writeData(dataOut);
        dim.writeData(dataOut);
        dataOut.writeFloat(prob);
        imageArea.writeData(dataOut);
    }


    public String toString()
    {
        return objectId + " " + type + " pos " + pos + " varPos " +  varPos + " vel " + vel + " varVel " + varVel + " dim " + dim + " prob " + prob + " imageArea " + imageArea;
    }
}
