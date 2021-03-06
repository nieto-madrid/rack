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
 *      Oliver Wulf      <oliver.wulf@web.de>
 *      Matthias Hentschel <hentschel@rts.uni-hannover.de>
 *
 */
package rack.gui.main;

import java.awt.*;
import java.awt.image.*;
import java.awt.event.*;
import java.io.*;
import java.util.*;

import javax.swing.*;
import javax.swing.event.PopupMenuEvent;
import javax.swing.event.PopupMenuListener;
import javax.imageio.*;

import rack.main.RackName;
import rack.main.defines.*;
import rack.drivers.*;
import rack.gui.GuiElement;
import rack.gui.GuiElementDescriptor;
import rack.navigation.*;

public class MapViewGui extends GuiElement implements MapViewInterface
{
    protected JPanel                       rootPanel;
    protected JPanel                       northPanel;
    protected MapViewComponent             mapComponent;
    
    protected JComboBox                    actionMenu;
    protected Vector<String>               actionCommands = new Vector<String>();
    protected Vector<MapViewInterface>     actionTargets = new Vector<MapViewInterface>();
    protected MapViewMouseListener         mouseListener = new MapViewMouseListener();
    protected ActionMenuListener           actionMenuListener = new ActionMenuListener();
    protected ActionListener    		   robotButtonAction;
    
    protected JRadioButton                 worldButton;
    protected JRadioButton                 chaseButton;
    protected JRadioButton                 robotButton;
    protected ButtonGroup                  viewGroup = new ButtonGroup();

    protected String                       positionUpdateCommand;
    
    protected BufferedImage                bgImg;
    protected int                          bgX;
    protected int                          bgY;
    protected int                          bgW;
    protected int                          bgH;
    protected boolean                      bgBicubic;

    protected int                          systemNumMax;
    protected int						   positionUpdate;
    protected int						   currRobotSys = 0;
    protected PositionProxy                positionProxy[];
    protected ChassisProxy                 chassisProxy[];
    protected ChassisParamMsg              chassisParam[];
    protected Vector<PositionDataMsg>      robotPosition;
    
    //protected int                          windowW  = 640; ////
    //protected int                          windowH  = 480; ////
    protected int                          oldRowCol[] = new int[2] ;
    protected int                          actRowCol[] = new int[2] ;
                        
    protected boolean                      usingMultipleImages = false;
    protected int                          workingResLevel = 0; 
    protected String                       globalInfoName = "info.txt";
    //local info:
    double                                 columns = 0.0;
    double                                 rows = 0.0;
    double                                 colW  = 0.0;
    double                                 rowH  = 0.0;
    double                                 ovlW  = 0;
    double                                 ovlH  = 0;
    double                                 resX;
    double                                 resY;
    double                                 origTfwX;
    double                                 origTfwY;
    String                                 fileNamePrefix;
    String                                 imageFormat;
    
    //global info   
    String                                 folderNamePrefix;
    int                                    resLevels;
    //  end of global info
    
    protected BufferedImage                bgImgLow;
    protected int                          bgXLow;
    protected int                          bgYLow;
    protected int                          bgWLow;
    protected int                          bgHLow;
    protected boolean                      bgBicubicLow;
    protected double                       oldZoom;
    protected int                          oldWorkingResLevel = -1; 
    protected double                       zoomLevels[];
    private int offsetX;
    private int offsetY;
    private int                            maxZoomRange = 1200000; 
    private int                            minZoomRange = 0;
    
    public MapViewGui(GuiElementDescriptor guiElement)
    {
        super(guiElement);
                
        String param = ge.getParameter("systemNumMax");
        if (param.length() > 0)
            systemNumMax = Integer.parseInt(param);
        else
            systemNumMax = 1;
        
        param = ge.getParameter("positionUpdate");
        if (param.length() > 0)
        {
        	positionUpdate = Integer.parseInt(param);
        }
        else
        {
        	positionUpdate = 1;
        }

        // init arrays
        positionProxy = new PositionProxy[systemNumMax];
        chassisProxy  = new ChassisProxy[systemNumMax];
        chassisParam  = new ChassisParamMsg[systemNumMax];
        
        // create MapView proxies
        for (int i = 0; i < systemNumMax; i++)
        {
        	if (positionUpdate == 1)
        	{
        		positionProxy[i] = (PositionProxy) mainGui.getProxy(RackName.POSITION, i, 0);
        	}
        	chassisProxy[i]  = (ChassisProxy)  mainGui.getProxy(RackName.CHASSIS, i, 0);

        	// get chassis parameter message
	        if (chassisProxy[i] != null)
	        {
	            chassisParam[i] = chassisProxy[i].getParam();
	        }
	        if (chassisParam[i] == null)
	        {
	            chassisParam[i] = new ChassisParamMsg();
	            chassisParam[i].boundaryFront 	 = 2600;
	            chassisParam[i].boundaryBack  	 = 600;
	            chassisParam[i].boundaryLeft  	 = 740;
	            chassisParam[i].boundaryRight 	 = 740;
	            chassisParam[i].safetyMargin  	 = 250;
	            chassisParam[i].safetyMarginMove = 1000;
	        }
        }

        // create MapView components
        mapComponent = new MapViewComponent(this);
        mapComponent.addMouseListener(mouseListener);
        mapComponent.addMouseMotionListener((MouseMotionListener)mouseListener);
        mapComponent.addMouseWheelListener((MouseWheelListener)mouseListener);
        mapComponent.setPreferredSize(new Dimension(600,400));
        mapComponent.setDefaultVisibleRange(50000.0);
        
        actionMenu = new JComboBox();
        actionMenu.addKeyListener(mapComponent.keyListener);
        actionMenu.addPopupMenuListener(actionMenuListener);

        worldButton = new JRadioButton("world", false);
        chaseButton = new JRadioButton("chase", true);
        robotButton = new JRadioButton("robot", false);
        viewGroup.add(worldButton);
        viewGroup.add(chaseButton);
        viewGroup.add(robotButton);
        worldButton.addKeyListener(mapComponent.keyListener);
        chaseButton.addKeyListener(mapComponent.keyListener);
        robotButton.addKeyListener(mapComponent.keyListener);
        
        robotButtonAction = new ActionListener() {
            public void actionPerformed(ActionEvent e)
            {
            	currRobotSys++;
            	
            	if (currRobotSys >= systemNumMax)
            	{
            		currRobotSys = 0;
            	}
            }
        };
        chaseButton.addActionListener(robotButtonAction);
        robotButton.addActionListener(robotButtonAction);
        

        JPanel buttonPanel = new JPanel(new GridLayout(1, 0));
        buttonPanel.add(worldButton);
        buttonPanel.add(chaseButton);
        buttonPanel.add(robotButton);

        // set MapView layout
        rootPanel = new JPanel(new BorderLayout(2, 2));
        rootPanel.setBorder(BorderFactory.createEmptyBorder(2, 2, 2, 2));

        northPanel = new JPanel(new BorderLayout(2, 2));
        northPanel.add(buttonPanel, BorderLayout.WEST);
        northPanel.add(actionMenu, BorderLayout.CENTER);
        northPanel.add(mapComponent.zoomPanel, BorderLayout.EAST);

        rootPanel.add(northPanel, BorderLayout.NORTH);
        rootPanel.add(mapComponent, BorderLayout.CENTER);
             
        // set MapView background
        param = ge.getParameter("bg");
        System.out.println("MapViewGui: background \"" + param + "\"\n");
        
        if (param.length() > 0)
        {
            String ext = param.substring(param.length()-3,param.length());
            if(ext.equals("txt"))
            {
                globalInfoName = param;
                System.out.println("\n     TRY TO READ GLOBAL INFO... \n\n");
                if (ReadGlobalInfo() <0)
                {
                    System.out.println("MapViewGui: Warning, could not read global info \n");
                }
                else
                {
                    System.out.println("USING MULTIPLE IMAGES\n");
                    usingMultipleImages = true;
                    System.out.println("   origTfwX: " + Double.toString( origTfwX) +
                                       "   origTfwY: " + Double.toString(origTfwY) + "\n\n\n");
                    oldRowCol = PositionToRowCol(0, 0);//d
                    
                    try
                    {
                        File file = new File(rowColToImageString(oldRowCol));
                        bgImg = ImageIO.read(file);
                    }
                    catch(Exception e)
                    {
                        
                    }
                    bgX = (int)(0.5*bgImg.getHeight()*Math.abs(resY)*1000.0) ;
                    bgY = (int)(0.5*bgImg.getWidth()*Math.abs(resX)*1000.0);
                    bgW = (int)((double)bgY*2.0);
                    bgH = (int)((double)bgX*2.0);
                    
                    try
                    {
                        File file = new File("big_eighth.bmp");
                        bgImgLow = ImageIO.read(file);
                    }
                    catch(Exception e)
                    {
                        
                    }
                    param = ge.getParameter("bgBicubic");
                    if (param.length() > 0)
                        if(Integer.parseInt(param) > 0)
                            bgBicubic = true;
                        else
                            bgBicubic = false;
                    else
                        bgBicubic = false;
                    
                    param = ge.getParameter("minZoomRange");
                    if (param.length() > 0)
                        minZoomRange = Integer.parseInt(param);
                    else
                        minZoomRange = 20000;
                    System.out.println(" *****minZoomRange="+minZoomRange);
                    
                    param = ge.getParameter("maxZoomRange");
                    if (param.length() > 0)
                        maxZoomRange = Integer.parseInt(param);
                    else
                        maxZoomRange = 1200000;
                    System.out.println(" *****maxZoomRange="+maxZoomRange);
                    
                    mapComponent.setBackgroundImage(0, bgImg, bgX, bgY, bgW, bgH, bgBicubic);
                    
                }
            }
            else{
            
                try
                {
                    File file = new File(param);
                    bgImg = ImageIO.read(file);
                }
                catch (Exception e)
                {
                    System.out.println("MapViewGui: Can't read background image \"" + param + "\"\n");
                }
                param = ge.getParameter("bgX");
                if (param.length() > 0)
                    bgX = Integer.parseInt(param);
                else
                    bgX = 0;

                param = ge.getParameter("bgY");
                if (param.length() > 0)
                    bgY = Integer.parseInt(param);
                else
                    bgY = 0;
                param = ge.getParameter("bgW");
                if (param.length() > 0)
                    bgW = Integer.parseInt(param);
                else
                    bgW = 100000;

                param = ge.getParameter("bgH");
                if (param.length() > 0)
                    bgH = Integer.parseInt(param);
                else
                    bgH = 100000;
                
                param = ge.getParameter("bgBicubic");
                if (param.length() > 0)
                    if(Integer.parseInt(param) > 0)
                        bgBicubic = true;
                    else
                        bgBicubic = false;
                else
                    bgBicubic = false;
                      
                mapComponent.setBackgroundImage(bgImg, bgX, bgY, bgW, bgH, bgBicubic);
            }
            oldZoom= mapComponent.zoomRange;              
        }
               
        worldButton.grabFocus();
    }
    
    public void addMapView(MapViewInterface mapView)
    {
        mapComponent.addMapView(mapView);
    }
    
    public void removeMapView(MapViewInterface mapView)
    {
        mapComponent.removeMapView(mapView);
    }
    
    public synchronized void addMapViewAction(String command, MapViewInterface mapView)
    {
        northPanel.remove(actionMenu);
        
        actionCommands.add(command);
        actionTargets.add(mapView);
        
        actionMenu = new JComboBox(actionCommands);
        actionMenu.addPopupMenuListener(actionMenuListener);
        
        northPanel.add(actionMenu, BorderLayout.CENTER);
    }
    
    public synchronized void removeMapViewActions(MapViewInterface mapView)
    {
        int index;

        northPanel.remove(actionMenu);
        
        while((index = actionTargets.indexOf(mapView)) >= 0)
        {
            actionCommands.removeElementAt(index);
            actionTargets.removeElementAt(index);
        }
        
        actionMenu = new JComboBox(actionCommands);
        actionMenu.addPopupMenuListener(actionMenuListener);
        
        northPanel.add(actionMenu, BorderLayout.CENTER);
    }
    
    public static MapViewGui findMapViewGui(GuiElementDescriptor ge)
    {
        Vector<GuiElementDescriptor> guiElements;
        guiElements = ge.getMainGui().getGuiElements();

        MapViewGui mapViewGui;
        
        for(int i = 0; i < guiElements.size(); i++)
        {
            try
            {
                mapViewGui = (MapViewGui) guiElements.get(i).getGui();
                if(mapViewGui != null)
                    return mapViewGui;
            }
            catch(ClassCastException e)
            {
            }
        }
        return null;
    }

    public void addRobotPosition(int robotSys, PositionDataMsg position)
    {
    	position.var.x = robotSys;
    	PositionDataMsg lastPosition = new PositionDataMsg();

    	for (int i = robotPosition.size()-1; i >= 0; i--)
        {
        	lastPosition = robotPosition.elementAt(i);
        	
        	if (lastPosition.var.x == robotSys)
        	{
                break;
        	}
        }

        if((position.recordingTime > lastPosition.recordingTime) ||
                robotPosition.isEmpty());
        {
            // add new position data
            robotPosition.add(position);
            
            while(robotPosition.size() > systemNumMax * 50)
            {
                robotPosition.removeElementAt(0);
            }
        }
    }
    
    public void repaint()
    {
        PositionDataMsg position = new PositionDataMsg();
       
    	for (int i = robotPosition.size()-1; i >= 0; i--)
        {
        	position = robotPosition.elementAt(i);
        	
        	if (position.var.x == currRobotSys)
        	{
                break;
        	}
        }        
        
        Position2d worldCenter;
        if(chaseButton.isSelected())
        {
            worldCenter = new Position2d(position.pos);
            worldCenter.rho = 0.0f;
        }
        else if(robotButton.isSelected())
        {
            worldCenter = new Position2d(position.pos);
        }
        else
        {
            worldCenter = new Position2d();
        }
        mapComponent.setWorldCenter(worldCenter);
        mapComponent.repaint();
    }

    public JComponent getComponent()
    {
        return rootPanel;
    }

    public void start()
    {
        zoomLevels = new double[resLevels];
        double m = (double)(maxZoomRange-minZoomRange)/(double)resLevels;
        
        int i;
        for(i=1; i<=resLevels; i++){
            //zoomLevels[i-1] = (double)i*(MAX_ZOOM_RANGE/(double)resLevels);
            double y = m*(i-1) + (double)minZoomRange; 
            zoomLevels[i-1] = y;//(double)i*(MAX_ZOOM_RANGE/(double)resLevels)/1.1;
            System.out.println("zoomLevels[" + (i-1) + "] = " + zoomLevels[i-1]);
        }
        if(i>2){
            zoomLevels[i-2] = (zoomLevels[i-3] + zoomLevels[i-2])/2;
        }
        robotPosition = mapComponent.getRobotPositionVector();  // update robot position
        
        addMapView(this);  // paint current robot position 
        
        addMapViewAction("Choose action command", this);
        positionUpdateCommand = "Position - update";
        addMapViewAction(positionUpdateCommand, this);
        
        if(usingMultipleImages)
        {
            //void
        }
        super.start();
    }
    
    public void run()
    {
        boolean res_change = false;
        while (terminate == false)
        {
            if(rootPanel.isShowing())
            {
                PositionDataMsg position;
                
                for (int i = 0; i < systemNumMax; i++)
                {
                	if(positionProxy[i] != null)
                	{   
                		position = positionProxy[i].getData();
                		if(position != null)
                		{
	                        //mapComponent.zoomInAction(1,3);
	                        //System.out.println("Zoom level: " + mapComponent.zoomRange);
	                        if(usingMultipleImages)
	                        {
	                            int j;
	                            for(j = 0;j<resLevels; j++)
	                            {
	                                workingResLevel = j;
	                                if(mapComponent.zoomRange < zoomLevels[j])
	                                {
	                                    break;
	                                }
	                                    
	                            }
	                            
	                            if(res_change = (oldWorkingResLevel != workingResLevel))
	                            {                                                                                  
	                                ReadLocalInfo(workingResLevel);
	                                oldWorkingResLevel = workingResLevel;
	                                System.out.println("Working Res Level: " + workingResLevel);
	                                System.out.println("Zoom level: " + mapComponent.zoomRange);
	
	                            };
	                            oldZoom = mapComponent.zoomRange;
	
	                            actRowCol = PositionToRowCol(mapComponent.getViewPortCenter().x, mapComponent.getViewPortCenter().y);
	
	                            if((actRowCol[0] != oldRowCol[0]) | (actRowCol[1] != oldRowCol[1]) | res_change)
	                            {
	                                res_change = false;
	                                try
	                                {
	                                    File file = new File(rowColToImageString(actRowCol));
	                                    System.out.println("Image File: " + rowColToImageString(actRowCol));
	                                    bgImg = ImageIO.read(file);
	
	                                }
	                                catch(Exception e)
	                                {
	                                    System.out.println("Error reading image file "+rowColToImageString(actRowCol));    
	                                }
	                                bgW = (int)( bgImg.getWidth() * 1000.0 * Math.abs(resY)) +500;
	                                bgH = (int)(bgImg.getHeight() * 1000.0 * Math.abs(resX));
	
	                                double d_row = (double)actRowCol[0]; //needed for avoiding rounding errors
	                                double d_col = (double)actRowCol[1]; //needed for avoiding rounding errors
	                                int ovlH_mm  = (int)(ovlH*1000.0*Math.abs(resY));
	                                int ovlW_mm  = (int)(ovlW*1000.0*Math.abs(resX));
	
	                                bgX = (int)( (rows - d_row - 1.0)*rowH*Math.abs(resY) )*1000 - ovlH_mm;
	                                bgX = Math.max(bgX,0);
	                                bgX = bgX + bgH/2;
	
	                                bgY = (int)( d_col*colW*Math.abs(resX)*1000.0) - ovlW_mm;
	                                bgY = Math.max(bgY,0);
	                                bgY = bgY + bgW/2;
	
	                                mapComponent.setBackgroundImage(0, bgImg, bgX+offsetX, bgY+offsetY, bgW, bgH, bgBicubic);
	                            }
	                            oldRowCol = actRowCol;
	                            if(!mapComponent.imageFillsFrame())
	                            {
	                                oldWorkingResLevel = workingResLevel;
	                                workingResLevel++;
	                            }
	                            /*else
	                                break;*/
	                            
	                        }
	                        addRobotPosition(i, position);
                		}
                    }
                }
                
                repaint();
                
            }
            
            try
            {
                sleep(500);
            }
            catch (InterruptedException e)
            {
            }
        }
        
        mapComponent.removeMapView(this);
        mapComponent.removeListener();
        mapComponent.removeMouseListener(mouseListener);
        mouseListener = null;
        actionMenu.removePopupMenuListener(actionMenuListener);
        actionMenuListener = null;
        mapComponent = null;

        rootPanel.removeAll();
        
        System.out.println("MapViewGui terminated");
    }

    public synchronized void paintMapView(MapViewGraphics mvg)
    {
    	for (int i = 0; i < systemNumMax; i++)
    	{
    		Graphics2D rg = mvg.getRobotGraphics(i);
    		paintRobot(rg, i);
    	}
    }

    public void mapViewActionPerformed(MapViewActionEvent event)
    {
        String command = event.getActionCommand();
        
        if (command != null)
        {
			if (command.equals(positionUpdateCommand)) {
				if (event.getEventId() == MapViewActionEvent.MOUSE_CLICKED_EVENT) {
					System.out.println("Position update "
							+ event.getWorldCursorPos());

					if (positionProxy != null) {
						Position3d position = new Position3d(event
								.getWorldCursorPos());

						positionProxy[0].update(new PositionDataMsg(position));
					}
				}
			}
		// else if(command.matches("Choose action command"))
		// {
		// System.out.println("action command " + event.getWorldCursorPos() +
		// " " + event.getRobotCursorPos() + " " + event.getRobotPosition());
		// }
        }
    }

    protected void paintRobot(Graphics2D g, int robotSys)
    {
        int chassisWidth  = chassisParam[robotSys].boundaryLeft + 
        				    chassisParam[robotSys].boundaryRight;
        int chassisLength = chassisParam[robotSys].boundaryBack + 
        					chassisParam[robotSys].boundaryFront;

        g.setColor(Color.LIGHT_GRAY);
        g.fillRect(-chassisParam[robotSys].boundaryBack - 
        			chassisParam[robotSys].safetyMargin,
                   -chassisParam[robotSys].boundaryLeft - 
                    chassisParam[robotSys].safetyMargin,
                    chassisLength + 2 * chassisParam[robotSys].safetyMargin + 
                    chassisParam[robotSys].safetyMarginMove,
                    chassisWidth + 2 * chassisParam[robotSys].safetyMargin);

        g.setColor(Color.GRAY);
        g.fillRect(-chassisParam[robotSys].boundaryBack,
                   -chassisParam[robotSys].boundaryLeft,
                    chassisLength, chassisWidth);

        g.setColor(Color.BLACK);
        g.drawRect(-chassisParam[robotSys].boundaryBack,
                   -chassisParam[robotSys].boundaryLeft,
                    chassisLength, chassisWidth);
        g.drawLine(0,
                   -chassisParam[robotSys].boundaryLeft,
                    chassisParam[robotSys].boundaryFront,
                    0);
        
        g.drawLine(chassisParam[robotSys].boundaryFront,
                   0,
                   0,
                   chassisParam[robotSys].boundaryRight);
    }
    
    public class MapViewMouseListener extends MouseAdapter implements MouseMotionListener, MouseWheelListener
    {
        public void mouseMoved(MouseEvent e)
        {	
            if(actionMenu.getSelectedIndex() > 0)
            {
          		String command;
                MapViewInterface mapView;

                synchronized(this)
                {
                    int index = actionMenu.getSelectedIndex();              
                    command = actionCommands.elementAt(index);
                    mapView = actionTargets.elementAt(index);
                }

                Position2d cursorPosition2d = mapComponent.getCursorPosition();
                PositionDataMsg robot = robotPosition.lastElement();
                Position2d robotPosition2d;
                robotPosition2d = new Position2d(robot.pos);
                
                if (cursorPosition2d!=null)
                {
                	MapViewActionEvent actionEvent = new MapViewActionEvent(MapViewActionEvent.MOUSE_MOVED_EVENT, command, cursorPosition2d, robotPosition2d);

                	mapView.mapViewActionPerformed(actionEvent);
                }
            }   
        }
        public void mouseDragged(MouseEvent e)
        {
        }
        public void mouseReleased(MouseEvent e)
        {
        }
        public void mousePressed(MouseEvent e)
        {
        }
        public void mouseClicked(MouseEvent e)
        {
            if(e.getClickCount() == 2)
            {
                String command;
                MapViewInterface mapView;

                synchronized(this)
                {
                    int index = actionMenu.getSelectedIndex();
                    
                    command = actionCommands.elementAt(index);
                    mapView = actionTargets.elementAt(index);
                }

                Position2d cursorPosition2d = mapComponent.getCursorPosition();
                PositionDataMsg robot = robotPosition.lastElement();
                Position2d robotPosition2d;
                robotPosition2d = new Position2d(robot.pos);
                if (cursorPosition2d!=null)
                {
                	MapViewActionEvent actionEvent = new MapViewActionEvent(MapViewActionEvent.MOUSE_CLICKED_EVENT, command, cursorPosition2d, robotPosition2d);

                	mapView.mapViewActionPerformed(actionEvent);
                }
            }
        }
        public void mouseWheelMoved(MouseWheelEvent e)
        {
            if(actionMenu.getSelectedIndex() > 0)
            {
          		String command;
                MapViewInterface mapView;

                synchronized(this)
                {
                    int index = actionMenu.getSelectedIndex();              
                    command = actionCommands.elementAt(index);
                    mapView = actionTargets.elementAt(index);
                }

                Position2d cursorPosition2d = mapComponent.getCursorPosition();
                PositionDataMsg robot = robotPosition.lastElement();
                Position2d robotPosition2d;
                robotPosition2d = new Position2d(robot.pos);

                if (cursorPosition2d!=null)
                {
	                MapViewActionEvent actionEvent = new MapViewActionEvent(MapViewActionEvent.MOUSE_WHEEL_EVENT, command, cursorPosition2d, robotPosition2d);
	
	                mapView.mapViewActionPerformed(actionEvent);
                }
            }   
        }
    }
    
    protected class ActionMenuListener implements PopupMenuListener
    {
        public void popupMenuCanceled(PopupMenuEvent arg0)
        {
        }

        public void popupMenuWillBecomeInvisible(PopupMenuEvent arg0)
        {
            System.out.println("invisible");
           
            if(actionMenu.getSelectedIndex() > 0)
            {
                mapComponent.showCursor = true;
                mapComponent.actionMenuIndex = actionMenu.getSelectedIndex();
            }
            else
            {
                mapComponent.showCursor = false;
            }

            worldButton.grabFocus();
        }

        public void popupMenuWillBecomeVisible(PopupMenuEvent arg0)
        {
            
        }
    };
    
    //new functions for the support of multi-image maps
    protected int[] PositionToRowCol(int x, int y) /*x and y in mm*/
    {
        x -= offsetX;
        y -= offsetY;
        
        int rowCol[] = new int[2];
        rowCol[0] = (int)Math.floor((rows - ((double)x + Math.abs(resY))/(rowH*Math.abs(resY)*1000.0))) ;
        rowCol[0] = Math.max(rowCol[0], 0);
           
        if(rowCol[0] >= (int)(rows))
        {
            rowCol[0] = (int)rows;
        }
        
        rowCol[1] = (int)Math.floor( (double)(y)/(Math.abs(resX)*colW*1000.0) );
        rowCol[1] = Math.max(rowCol[1], 0);
        
        if(rowCol[1] >= columns)
        {
            rowCol[1] = 0;
        }
        
        return rowCol;
        
    };
    
    protected String rowColToImageString(int row, int col)
    {
        String zero = "0";
        return folderNamePrefix + "_" + workingResLevel + "/" +
               fileNamePrefix + zero.substring(0, 2-Integer.toString(row).length()) +
               Integer.toString(row) + "_" + zero.substring(0, 2-Integer.toString(col).length()) +
               Integer.toString(col) + "." + imageFormat;
        
    };
    
    protected String rowColToImageString(int rowCol[])
    {
        String zero = "0";
        int row = rowCol[0];
        int col = rowCol[1];
        
        return folderNamePrefix + "_" + workingResLevel + "/" +
               fileNamePrefix + zero.substring(0, 2-Integer.toString(row).length()) +
               Integer.toString(row) + "_" + zero.substring(0, 2-Integer.toString(col).length()) +
               Integer.toString(col) + "." + imageFormat;
    };
    
    protected int ReadGlobalInfo()
    {
        String str;
        int    result = -1;
        String exception_string = globalInfoName;
        //double ImageW;
        //double ImageH;
        
        try {
            BufferedReader in = new BufferedReader(new FileReader(globalInfoName));//workingFolder+"info.txt"
            
            if (!in.ready())
            {
                exception_string = "BufferedReader in.read failed ("+globalInfoName+")";
                throw new IOException();           //and show error message
            }
            
            if((str = in.readLine()) == null){
                exception_string = "Could not read first line";
                throw new IOException();
            }
            folderNamePrefix = str;
            
            if((str = in.readLine()) == null){
                exception_string = "Could not read second line";
                throw new IOException();
            }
            resLevels = Integer.parseInt(str);
            
            if((str = in.readLine()) == null){
                exception_string = "Could not read third line";
                throw new IOException();
            }
            //ImageW = Double.parseDouble(str);
            
            if((str = in.readLine()) == null){
                exception_string = "Could not read fourth line";
                throw new IOException();
            }
            //ImageH = Double.parseDouble(str);
            in.close();
                      
        } 
        catch (IOException e) {
             System.out.println("Error Reading GlobalInfo: "+exception_string);
             System.out.println("foldernameprefix"+folderNamePrefix);
             return -1;
        }
        
        if((result = ReadLocalInfo(workingResLevel)) != 1)
        {
            System.out.println("Error@localinfo =========== "+Integer.toString(result)+" \n");
        }
        else
        {
            PositionDataMsg position;
            PositionGkDataMsg posGK = new PositionGkDataMsg();
            posGK.easting  = origTfwX * 1000.0;
            posGK.northing = origTfwY * 1000.0;
            position = positionProxy[0].gkToPos(posGK);
        
            if (position != null)
            {
                offsetX = position.pos.x;  
                offsetY = position.pos.y;
            }
            else
            {
                offsetX = 0;
                offsetY = 0;
            }
            System.out.println(">> Offset X: " + offsetX);
            System.out.println(">> Offset Y: " + offsetY);
        }
        return result;
    };
    
    protected int ReadLocalInfo(int ResLevel)
    {
        String str;
        int    result = -1;
        double bigImageW;
        double bigImageH;
        
        try{
            str = folderNamePrefix + "_" + ResLevel + "/localinfo.txt";
            BufferedReader in = new BufferedReader(new FileReader(str));
            if (!in.ready())
            {
                throw new IOException();           
            }
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            bigImageW = Double.parseDouble(str);
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            bigImageH = Double.parseDouble(str);
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            colW = Double.parseDouble(str);
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            rowH = Double.parseDouble(str);
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            fileNamePrefix = str;
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            imageFormat = str;
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            ovlW = Double.parseDouble(str);
            
            if((str = in.readLine()) == null){
                throw new IOException();
            }
            ovlH = Double.parseDouble(str);
            
            in.close();
            rows    = bigImageH / rowH;
            columns = bigImageW / colW;
            
            try{
                str = folderNamePrefix + "_" + ResLevel + "/" + fileNamePrefix + "00_00.tfw";
                //System.out.println(str);
                in = new BufferedReader(new FileReader(str));

                if (!in.ready())
                {
                    throw new IOException();           //and show error message
                }

                if((str = in.readLine()) == null){
                    throw new IOException();
                }
                resX = Double.parseDouble(str);

                if((str = in.readLine()) == null){
                    throw new IOException();
                }
                
                if((str = in.readLine()) == null){
                    throw new IOException();
                }

                if((str = in.readLine()) == null){
                    throw new IOException();
                }resY = Double.parseDouble(str);

                if((str = in.readLine()) == null){
                    throw new IOException();
                }
                origTfwX = Double.parseDouble(str);

                if((str = in.readLine()) == null){
                    throw new IOException();
                }
                origTfwY = Double.parseDouble(str) + rows*rowH*resY;
                result = 1;
                in.close();
            }

            catch(IOException e){
                result = -1;
                System.out.println("ReadlocalInfo@MapViewGui: Read of TFW file failed\n");
            }
        }
        catch(IOException e)
        {
            result = -1;
            System.out.println("ReadLocalInfo@MapViewGui: Read of localinfo.txt failed \n");
        }
        
        return result;
    }
    
    protected double to_mm()
    {
        return 1.0;
    }
    
    public void removeBackgroundImage()
    {
    	mapComponent.removeBackgroundImage();
    }
    
    public void setBackgroundImage(int index, BufferedImage bgImg, int bgX, int bgY, int bgW, int bgH, boolean bgBicubic)
    {
    	mapComponent.setBackgroundImage(index, bgImg, bgX, bgY, bgW, bgH, bgBicubic);
    }
    
    public Rectangle getViewPort()
    {
    	return mapComponent.getViewPort();
    }
}
