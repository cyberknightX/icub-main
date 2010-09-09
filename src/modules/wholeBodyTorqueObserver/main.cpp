/**
@ingroup icub_module

\defgroup wholeBodyTorqueObserver wholeBodyTorqueObserver
 
Estimates the external forces and torques acting at the end effector
through a model based estimation of the robot dynamics
 
Copyright (C) 2008 RobotCub Consortium
 
Author: Matteo Fumagalli
 
Date: first release 8/07/2010 

CopyPolicy: Released under the terms of the GNU GPL v2.0.

\section intro_sec Description

This module estimates the external wrench acting at the end
effector of the iCub limbs, through a model based compensation 
of the 6-axis force/torque (FT) sensor's measurements, which are 
acquired through an input YARP port and provides them to an 
output YARP ports.
The estimation is perfomed relying on rigid body dynamics using CAD 
parameters. 
The intrinsic offsets of the sensors, which are due to the stresses
generated during mounting, are defined by the first FT data. In the 
future it will also be given the possibility to set the offsets of 
the sensors.
The model of the sensor measurements consider a fixed base, with z-axis 
pointing upwards. The estimation of the external wrench applied at the 
end-effector of the limb has the same orientation of the fixed base frame.
 
\section lib_sec Libraries 
- YARP libraries. 
- ctrlLib library. 
- iKin library.
- iDyn library.  

\section parameters_sec Parameters

--name \e name 
- The parameter \e name identifies the module's name; all the 
  open ports will be tagged with the prefix <name>/. If not
  specified \e /ftObs is assumed.

  --robot
- The parameter \e robot identifies the robot that is used. This parameter defines the
  prefix of the ports of the device. As default \e icub is used. 

--part  
- The parameter \e part identifies the part of the robot which is used. All the opened 
  ports will deal with the part which is defined. the default value is \e left_arm 

--rate \e r 
- The parameter \e r identifies the rate the thread will work. If not
  specified \e 100ms is assumed. The minimum suggested rate is \e 20ms.

\section portsa_sec Ports Accessed
The port the service is listening to.

\section portsc_sec Ports Created
 
- \e <name>/<part>/FT:i (e.g. /ftObs/right_arm/FT:i) receives the input data 
  vector.
 
- \e <name>/<part>/wrench:o (e.g. /ftObs/right_arm/wrench:o) provides the estimated 
  end-effector wrench.

\section in_files_sec Input Data Files
None.

\section out_data_sec Output Data Files
None. 
 
\section conf_file_sec Configuration Files
None
 
\section tested_os_sec Tested OS
Linux and Windows.

\section example_sec Example
By launching the following command: 
 
\code 
wholeBodyTorqueObserver --name ftObs --robot icub --part right_arm --rate 50  
\endcode 
 
the module will create the listening port /ftObs/right_arm/FT:i for 
the acquisition of data vector coming for istance from the right arm analog port.
At the same time it will provide the estimated 
external wrench at the end-effector to /ftObs/right_arm/wrench:o port. (use --help option to 
see). 
 
Try now the following: 
 
\code 
yarp connect /icub/right_arm/analog:o /ftObs/right_arm/FT:i
\endcode 
 
\author Matteo Fumagalli

This file can be edited at \in src/wholeBodyTorqueObserver/main.cpp.
*/ 

#include <yarp/os/BufferedPort.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Time.h>
#include <yarp/os/Network.h>
#include <yarp/os/RateThread.h>
#include <yarp/os/Stamp.h>
#include <yarp/sig/Vector.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/ControlBoardInterfaces.h>
#include <iCub/ctrl/ctrlMath.h>
#include <iCub/ctrl/adaptWinPolyEstimator.h>
#include <iCub/iDyn/iDyn.h>
#include <iCub/iDyn/iDynBody.h>

#include <iostream>
#include <iomanip>
#include <string.h>

using namespace yarp;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace yarp::dev;
using namespace iCub::ctrl;
using namespace iCub::iDyn;
using namespace std;

//performance test 
//--------------------------------------
#define VOCAB_TEST VOCAB4('t','e','s','t')
#define VOCAB_COMP VOCAB4('c','o','m','p')
//--------------------------------------

#define MAX_JN 12
#define MAX_FILTER_ORDER 6
enum thread_status_enum {STATUS_OK=0, STATUS_DISCONNECTED}; 

double lpf_ord1_3hz(double input, int j)
{ 
	if (j<0 || j>= MAX_JN)
	{
		cout<<"Received an invalid joint index to filter"<<endl;
		return 0;
	}

	static double xv[MAX_FILTER_ORDER][MAX_JN];
	static double yv[MAX_FILTER_ORDER][MAX_JN];
	xv[0][j] = xv[1][j] ; 
    xv[1][j] = input / 1.870043440e+01;
    yv[0][j] = yv[1][j] ; 
    yv[1][j] =   (xv[0][j]  + xv[1][j] ) + (  0.8930506128 * yv[0][j] );
    return (yv[1][j]);
}
// class inverseDynamics: class for reading from Vrow and providing FT on an output port
class inverseDynamics: public RateThread
{
private:
    PolyDriver *ddAL;
    PolyDriver *ddAR;
    PolyDriver *ddH;
    IEncoders  *iencs_arm_left;
    IEncoders  *iencs_arm_right;
    IEncoders  *iencs_head;
	
    PolyDriver *ddLL;
    PolyDriver *ddLR;
    PolyDriver *ddT;
    IEncoders  *iencs_leg_left;
    IEncoders  *iencs_leg_right;
    IEncoders  *iencs_torso;

    string part;

    Vector *ft_arm_left;
    Vector *ft_arm_right;
    Vector *inertial;
    BufferedPort<Vector> *port_ft_arm_left;
    BufferedPort<Vector> *port_ft_arm_right;
	BufferedPort<Vector> *port_inertial_thread;

	BufferedPort<Bottle> *port_RATorques;
	BufferedPort<Bottle> *port_RLTorques;
	BufferedPort<Bottle> *port_LATorques;
	BufferedPort<Bottle> *port_LLTorques;

    Vector *ft_leg_left;
    Vector *ft_leg_right;
    BufferedPort<Vector> *port_ft_leg_left;
    BufferedPort<Vector> *port_ft_leg_right;
    bool first;
	thread_status_enum thread_status;

    AWLinEstimator  *InertialEst;
    AWLinEstimator  *linEstUp;
    AWQuadEstimator *quadEstUp;
    AWLinEstimator  *linEstLow;
    AWQuadEstimator *quadEstLow;

    int ctrlJnt;
	int allJnt;
	iCubWholeBody icub;
	iCubWholeBody icub_sens;

    Vector encoders_arm_left;
    Vector encoders_arm_right;
    Vector encoders_head;

    Vector encoders_leg_left;
    Vector encoders_leg_right;
    Vector encoders_torso;

	Vector q_head, dq_head, d2q_head;
	Vector q_larm, dq_larm, d2q_larm;
	Vector q_rarm, dq_rarm, d2q_rarm;
	Vector all_q_up, all_dq_up, all_d2q_up;

	Vector q_torso, dq_torso, d2q_torso;
	Vector q_lleg, dq_lleg, d2q_lleg;
	Vector q_rleg, dq_rleg, d2q_rleg;
	Vector all_q_low, all_dq_low, all_d2q_low;

    Vector w0,dw0,d2p0,Fend,Muend;
    Vector F_LArm, F_RArm, F_iDyn_LArm, F_iDyn_RArm, Offset_LArm, Offset_RArm;
    Vector F_LLeg, F_RLeg, F_iDyn_LLeg, F_iDyn_RLeg, Offset_LLeg, Offset_RLeg;
	Matrix F_sens_up, F_sens_low, F_ext_up, F_ext_low;
	Vector inertial_measurements;

	int test;
    double startRun;
    double endRun;
    double startCompute;
    double endCompute;
    double ftRead;
    double ftCur;
    int ftNew;
    //ports for sending information
    BufferedPort<Bottle> *port_perf_test;
    BufferedPort<Bottle> *port_perf_test_ftRead;
    Bottle infoTest;

    // icub model
	int comp;
    Matrix FM_sens_up,FM_sens_low;
    BufferedPort<Bottle> *port_compare_test;
    Bottle compareTest;

    Vector evalVelUp(const Vector &x)
    {
        AWPolyElement el;
        el.data=x;
        el.time=Time::now();

        return linEstUp->estimate(el);
    }
	Vector evalVelLow(const Vector &x)
    {
        AWPolyElement el;
        el.data=x;
        el.time=Time::now();

        return linEstLow->estimate(el);
    }
	Vector eval_domega(const Vector &x)
    {
        AWPolyElement el;
        el.data=x;
        el.time=Time::now();

        return InertialEst->estimate(el);
    }

    Vector evalAccUp(const Vector &x)
    {
        AWPolyElement el;
        el.data=x;
        el.time=Time::now();

        return quadEstUp->estimate(el);
    }
    Vector evalAccLow(const Vector &x)
    {
        AWPolyElement el;
        el.data=x;
        el.time=Time::now();

        return quadEstLow->estimate(el);
    }
	void init_upper()
	{
		//---------------------PARTS-------------------------//
		// Left_arm variables
		allJnt = 0;
        int jnt=0;
        iencs_arm_left->getAxes(&jnt);
        encoders_arm_left.resize(jnt);
        F_LArm.resize(6,0.0);
        F_iDyn_LArm.resize(6,0.0);
        Offset_LArm.resize(6,0.0);
		q_larm.resize(7,0.0);
		dq_larm.resize(7,0.0);
		d2q_larm.resize(7,0.0);
		allJnt+=jnt;

		// Right_arm variables
		jnt = 0;
        iencs_arm_right->getAxes(&jnt);
        encoders_arm_right.resize(jnt);
		q_rarm.resize(7,0.0);
		dq_rarm.resize(7,0.0);
		d2q_rarm.resize(7,0.0);
        F_RArm.resize(6,0.0);
        F_iDyn_RArm.resize(6,0.0);
        Offset_RArm.resize(6,0.0);
		allJnt+=jnt;

		// Head variables
		jnt = 0;
        iencs_head->getAxes(&jnt);
        encoders_head.resize(jnt);
		q_head.resize(3,0.0);
		dq_head.resize(3,0.0);
		d2q_head.resize(3,0.0);
		allJnt+=jnt;

		all_q_up.resize(allJnt,0.0);
		all_dq_up.resize(allJnt,0.0);
		all_d2q_up.resize(allJnt,0.0);
		F_sens_up.zero(); 
        FM_sens_up.resize(6,2); FM_sens_up.zero();
	}	
	void init_lower()
	{
		//---------------------PARTS-------------------------//
		// Left_arm variables
		allJnt = 0;
        int jnt=0;
        iencs_leg_left->getAxes(&jnt);
        encoders_leg_left.resize(jnt);
        F_LLeg.resize(6,0.0);
        F_iDyn_LLeg.resize(6,0.0);
        Offset_LLeg.resize(6,0.0);
		q_lleg.resize(7,0.0);
		dq_lleg.resize(7,0.0);
		d2q_lleg.resize(7,0.0);
		allJnt+=jnt;

		// Right_leg variables
		jnt = 0;
        iencs_leg_right->getAxes(&jnt);
        encoders_leg_right.resize(jnt);
		q_rleg.resize(7,0.0);
		dq_rleg.resize(7,0.0);
		d2q_rleg.resize(7,0.0);
        F_RLeg.resize(6,0.0);
        F_iDyn_RLeg.resize(6,0.0);
        Offset_RLeg.resize(6,0.0);
		allJnt+=jnt;

		// Head variables
		jnt = 0;
        iencs_torso->getAxes(&jnt);
        encoders_torso.resize(jnt);
		q_torso.resize(3,0.0);
		dq_torso.resize(3,0.0);
		d2q_torso.resize(3,0.0);
		allJnt+=jnt;

		all_q_low.resize(allJnt,0.0);
		all_dq_low.resize(allJnt,0.0);
		all_d2q_low.resize(allJnt,0.0);

		F_sens_low.zero();
        FM_sens_low.resize(6,2); FM_sens_low.zero();
	}

public:
    inverseDynamics(int _rate, PolyDriver *_ddAL, PolyDriver *_ddAR, PolyDriver *_ddH, PolyDriver *_ddLL, PolyDriver *_ddLR, PolyDriver *_ddT) : RateThread(_rate), ddAL(_ddAL), ddAR(_ddAR), ddH(_ddH), ddLL(_ddLL), ddLR(_ddLR), ddT(_ddT)
    {        
        first = true;
		test = 0;
		comp = 0;
        //---------------------PORT--------------------------//
		
		port_inertial_thread=new BufferedPort<Vector>;
        port_ft_arm_left=new BufferedPort<Vector>;
        port_ft_arm_right=new BufferedPort<Vector>;
        port_ft_leg_left=new BufferedPort<Vector>;
        port_ft_leg_right=new BufferedPort<Vector>;
		port_RATorques = new BufferedPort<Bottle>;
		port_LATorques = new BufferedPort<Bottle>;
		port_RLTorques = new BufferedPort<Bottle>;
		port_LLTorques = new BufferedPort<Bottle>;
		
        port_inertial_thread->open("/wholeBodyTorqueObserver/inertial:i");
		port_ft_arm_left->open("/wholeBodyTorqueObserver/left_arm/FT:i");
		port_ft_arm_right->open("/wholeBodyTorqueObserver/right_arm/FT:i");
		port_ft_leg_left->open("/wholeBodyTorqueObserver/left_leg/FT:i");
		port_ft_leg_right->open("/wholeBodyTorqueObserver/right_leg/FT:i");
		port_RATorques->open("/wholeBodyTorqueObserver/right_arm/Torques:o");
		port_LATorques->open("/wholeBodyTorqueObserver/left_arm/Torques:o");
		port_RLTorques->open("/wholeBodyTorqueObserver/right_leg/Torques:o");
		port_LLTorques->open("/wholeBodyTorqueObserver/left_leg/Torques:o");


		//---------------------DEVICES--------------------------//
        ddAL->view(iencs_arm_left);
        ddAR->view(iencs_arm_right);
		ddH->view(iencs_head);
		ddLL->view(iencs_leg_left);
        ddLR->view(iencs_leg_right);
		ddT->view(iencs_torso);
		
        linEstUp =new AWLinEstimator(16,1.0);
        quadEstUp=new AWQuadEstimator(25,1.0);
	    linEstLow =new AWLinEstimator(16,1.0);
        quadEstLow=new AWQuadEstimator(25,1.0);
		InertialEst = new AWLinEstimator(16,1.0);

		//-----------parts INIT VARIABLES----------------//
		init_upper();
		init_lower();
		//-----------CARTESIAN INIT VARIABLES----------------//
		
		w0.resize(3,0.0);
		dw0.resize(3,0.0);
		d2p0.resize(3,0.0);
		Fend.resize(3,0.0);
		Muend.resize(3,0.0);
		F_ext_up.resize(6,3);
		F_ext_up = 0.0;
		F_ext_low.resize(6,3);
		F_ext_low = 0.0;
		inertial_measurements.resize(12);
		inertial_measurements.zero();
  
		//--------------TEST VARIABLES----------------//
        startRun=0.0;
        endRun=0.0;
        startCompute=0.0;
        endCompute=0.0;
        ftRead=0.0;

        port_perf_test = new BufferedPort<Bottle>;
        port_perf_test_ftRead = new BufferedPort<Bottle>;
        port_compare_test = new BufferedPort<Bottle>;  
        port_perf_test->open("/wholeBodyTorqueObserver/performance/times:o");
        port_perf_test_ftRead->open("/wholeBodyTorqueObserver/performance/ftread:o");
		port_compare_test->open("/wholeBodyTorqueObserver/performance/fterr:o");


    }

    bool threadInit()
    {   
		calibrateOffset(10);
		thread_status = STATUS_OK;
        return true;
    }

	inline thread_status_enum getThreadStatus() 
	{
		return thread_status;
	}

    void run()
    {   
		if(test==VOCAB_TEST)
		{
			startRun = Time::now();
			ftNew = 0;
		}
        static unsigned long int alive_counter = 0;
        static double curr_time = Time::now();
        if (Time::now() - curr_time > 60)
        {
            printf ("wholeBodyTorqueObserver is alive! running for %ld mins.\n",++alive_counter);
            curr_time = Time::now();
        }
		thread_status = STATUS_OK;
        if(readAndUpdate(false) == false)
		{
			printf ("inverseDynamics thread lost connection with iCubInterface.\n");
			thread_status = STATUS_DISCONNECTED;
		}
		setZeroJntAngVelAcc();
		setUpperMeasure();
		setLowerMeasure();

        if(ft_arm_left!=0)  F_LArm = -1.0 * (*ft_arm_left-Offset_LArm);
        if(ft_arm_right!=0) F_RArm = -1.0 * (*ft_arm_right-Offset_RArm);
        if(ft_leg_left!=0)  F_LLeg = -1.0 * (*ft_leg_left-Offset_LLeg);
        if(ft_leg_right!=0) F_RLeg = -1.0 * (*ft_leg_right-Offset_RLeg);

		Vector F_up(6);
		F_up=0.0;
		if(test==VOCAB_TEST)
			startCompute = Time::now();
		icub.upperTorso->update(w0,dw0,d2p0,F_RArm,F_LArm,F_up);
		icub.lowerTorso->update(icub.upperTorso->getTorsoAngVel(), 
								icub.upperTorso->getTorsoAngAcc(),
								icub.upperTorso->getTorsoLinAcc(),
								F_RLeg,F_LLeg,F_up);
		if(test==VOCAB_TEST)
			endCompute = Time::now();

		Vector LATorques = icub.upperTorso->getTorques("left_arm");
		Vector RATorques = icub.upperTorso->getTorques("right_arm");
		Vector HDTorques = icub.upperTorso->getTorques("head");
		
		Vector LLTorques = icub.lowerTorso->getTorques("left_leg");
		Vector RLTorques = icub.lowerTorso->getTorques("right_leg");
		Vector TSTorques = icub.lowerTorso->getTorques("torso");
				
		writeTorque(RLTorques, 2, port_RLTorques);
		writeTorque(LLTorques, 2, port_LLTorques);
		writeTorque(RATorques, 1, port_RATorques);
		writeTorque(LATorques, 1, port_LATorques);

		
		if(test==VOCAB_TEST)
		{		
			endRun = Time::now();
			//now we send everything to a specific port
			infoTest.clear();
			infoTest.addInt(ftNew); // if there's a new ft value or not
			infoTest.addDouble(startRun);
			infoTest.addDouble(endRun-startRun);
			infoTest.addDouble(endCompute-startCompute);
			port_perf_test->prepare() = infoTest;
			port_perf_test->write();

			infoTest.clear();
			infoTest.addInt(ftNew);
			infoTest.addDouble(startRun);
			infoTest.addDouble(endRun-ftCur);
			infoTest.addDouble(ftCur-ftRead);
			port_perf_test_ftRead->prepare() = infoTest;
			port_perf_test_ftRead->write();
		}
		if(comp==VOCAB_COMP)
		{
			// computations are performed here: because they are extra computations which must not be included in 
			// the count of thread rate and computation time (that part is only for whole body torque observer)
		    
			icub_sens.upperTorso->setInertialMeasure(w0,dw0,d2p0);
			FM_sens_up = icub_sens.upperTorso->estimateSensorsWrench(F_ext_up,false);
			icub_sens.lowerTorso->setInertialMeasure(icub_sens.upperTorso->getTorsoAngVel(),icub_sens.upperTorso->getTorsoAngAcc(),icub_sens.upperTorso->getTorsoLinAcc());
			FM_sens_low = icub_sens.lowerTorso->estimateSensorsWrench(F_ext_low,false);

			// send sensor measures, estimation etc to port
			compareTest.clear();
			compareTest.addInt(ftNew);
			compareTest.addDouble(startRun);
			//the FT sensor wrench estimated by the model: note the -1.0 before is necessary!
			for(int i=0; i<6;i++)
				compareTest.addDouble(-(FM_sens_up.getCol(0))[i]); //RA
			for(int i=0; i<6;i++)
				compareTest.addDouble(-(FM_sens_up.getCol(1))[i]); //LA
			for(int i=0; i<6;i++)
				compareTest.addDouble(-(FM_sens_low.getCol(0))[i]); //RL
			for(int i=0; i<6;i++)
				compareTest.addDouble(-(FM_sens_low.getCol(1))[i]); //LL
			//the real FT sensor wrench (measured)
			for(int i=0; i<6;i++)
				compareTest.addDouble(F_RArm[i]);
			for(int i=0; i<6;i++)
				compareTest.addDouble(F_LArm[i]);
			for(int i=0; i<6;i++)
				compareTest.addDouble(F_RLeg[i]);
			for(int i=0; i<6;i++)
				compareTest.addDouble(F_LLeg[i]);

			port_compare_test->prepare() = compareTest;
			port_compare_test->write();
		}
    }

    void threadRelease()
    {
		fprintf(stderr, "Closing the linest\n");
		if(linEstUp)
		{
			delete linEstUp;
			linEstUp = 0;
		}
		if(linEstLow)
		{
			delete linEstLow;
			linEstLow = 0;
		}
		fprintf(stderr, "Closing the quadEst\n");
		if(quadEstUp)
		{
			delete quadEstUp;
			quadEstUp = 0;
		}
		if(quadEstLow)
		{
			delete quadEstLow;
			quadEstLow = 0;
		}
		fprintf(stderr, "Closing the InertialEest\n");
		if(InertialEst)
		{
			delete InertialEst;
			InertialEst = 0;
		}

		fprintf(stderr, "Closing the RATorques\n");
		closePort(port_RATorques);
		fprintf(stderr, "Closing the LATorques\n");
		closePort(port_LATorques);
		fprintf(stderr, "Closing the RLTorques\n");
		closePort(port_RLTorques);
		fprintf(stderr, "Closing the LLTorques\n");
		closePort(port_LLTorques);

		fprintf(stderr, "Closing the inertial\n");
		closePort(port_inertial_thread);
		fprintf(stderr, "Closing the ft_arm_right\n");
		closePort(port_ft_arm_right);
		fprintf(stderr, "Closing the ft_arm_left\n");
		closePort(port_ft_arm_left);

	  
//#if PERFORMANCE_TEST
	  
		fprintf(stderr, "Closing the performance test port - times\n");
		closePort(port_perf_test);
		fprintf(stderr, "Closing the performance test port - ftread\n");
		closePort(port_perf_test_ftRead);
	
//#endif

//#if COMPARE_TEST
	 
		fprintf(stderr, "Closing the compare test port - fterr\n");
		closePort(port_compare_test);
	  
//#endif
    }   

	void closePort(Contactable *_port)
	{
		if (_port)
        {
            _port->interrupt();
            _port->close();

            delete _port;
            _port = 0;
        }
	}

	void writeTorque(Vector _values, int _address, BufferedPort<Bottle> *_port)
	{
		Bottle a;
		a.addInt(_address);
		for(int i=0;i<_values.length();i++)
			a.addDouble(_values(i));
		_port->prepare() = a;
		_port->write();
	}

	void calibrateOffset(const unsigned int Ntrials)
	{
		fprintf(stderr,"SensToTorques: starting sensor offset calibration .. \n\n");

		Offset_LArm.zero();
		Offset_RArm.zero();
		Offset_LLeg.zero();
		Offset_RLeg.zero();
			
		// N trials to get a more accurate estimation
		for(unsigned int i=0; i<Ntrials; i++)
		{
			//read joints and ft sensor
			readAndUpdate(true,true);

			/*
			// TO BE VERIEFIED IF USEFUL
			setZeroJntAngVelAcc();
			setUpperMeasure(true);
			setLowerMeasure(true);
			*/

            icub_sens.upperTorso->setInertialMeasure(w0,dw0,d2p0);
			Matrix F_sensor_up = icub_sens.upperTorso->estimateSensorsWrench(F_ext_up,false);
			icub_sens.lowerTorso->setInertialMeasure(icub_sens.upperTorso->getTorsoAngVel(),icub_sens.upperTorso->getTorsoAngAcc(),icub_sens.upperTorso->getTorsoLinAcc());
			Matrix F_sensor_low = icub_sens.lowerTorso->estimateSensorsWrench(F_ext_low,false);
		
			F_iDyn_LArm  = -1.0 * F_sensor_up.getCol(1);
			F_iDyn_RArm = -1.0 * F_sensor_up.getCol(0);
			F_iDyn_LLeg  = -1.0 * F_sensor_low.getCol(1);
			F_iDyn_RLeg = -1.0 * F_sensor_low.getCol(0);

			F_RArm = *ft_arm_right;
			F_LArm = *ft_arm_left;
			F_RLeg = *ft_leg_right;
			F_LLeg = *ft_leg_left;

			Offset_LArm = Offset_LArm + (F_LArm-F_iDyn_LArm);
			Offset_RArm = Offset_RArm + (F_RArm-F_iDyn_RArm);
			Offset_LLeg = Offset_LLeg + (F_LLeg-F_iDyn_LLeg);
			Offset_RLeg = Offset_RLeg + (F_RLeg-F_iDyn_RLeg);
		}
		fprintf(stderr,"!\n");
		fprintf(stderr, "Ntrials: %d\n", Ntrials);
		fprintf(stderr, "F_LArm: %s\n", F_LArm.toString().c_str());
		fprintf(stderr, "F_idyn_LArm: %s\n", F_iDyn_LArm.toString().c_str());
		fprintf(stderr, "F_RArm: %s\n", F_RArm.toString().c_str());
		fprintf(stderr, "F_idyn_RArm: %s\n", F_iDyn_RArm.toString().c_str());		
		fprintf(stderr, "F_LLeg: %s\n", F_LLeg.toString().c_str());
		fprintf(stderr, "F_idyn_LLeg: %s\n", F_iDyn_LLeg.toString().c_str());		
		fprintf(stderr, "F_RLeg: %s\n", F_RLeg.toString().c_str());
		fprintf(stderr, "F_idyn_RLeg: %s\n", F_iDyn_RLeg.toString().c_str());
		Offset_LArm = 1.0/(double)Ntrials * Offset_LArm;
		cout<<"Left Arm:	"<<Offset_LArm.toString()<<endl;
		Offset_RArm = 1.0/(double)Ntrials * Offset_RArm;
		cout<<"Right Arm:	"<<Offset_RArm.toString()<<endl;
		Offset_LLeg = 1.0/(double)Ntrials * Offset_LLeg;
		cout<<"Left Leg:	"<<Offset_LLeg.toString()<<endl;
		Offset_RLeg = 1.0/(double)Ntrials * Offset_RLeg;
		cout<<"Right Leg:	"<<Offset_RLeg.toString()<<endl;

		//Time::delay(2.0);
	}
	bool readAndUpdate(bool waitMeasure=false, bool _init=false)
	{
		bool b = true;
		
		ft_arm_left  = port_ft_arm_left->read(waitMeasure);
        ft_arm_right = port_ft_arm_right->read(waitMeasure);
        ft_leg_left  = port_ft_leg_left->read(waitMeasure);
        ft_leg_right = port_ft_leg_right->read(waitMeasure);
		inertial = port_inertial_thread->read(waitMeasure);
		
		if(test==VOCAB_TEST)
		{
			ftCur=Time::now();
			if((ft_arm_left!=NULL)&&(ft_arm_right!=NULL)&&(ft_leg_left!=NULL)&&(ft_leg_right!=NULL))
			{
				ftNew=1;
				ftRead=ftCur;
			}
			else
				ftNew=0;
		}

		int sz = 0;
		if(inertial!=0)
		{
			sz = inertial->length();
			inertial_measurements.resize(sz) ;
			inertial_measurements= *inertial;
			d2p0[0] = inertial_measurements[0];
			d2p0[1] = inertial_measurements[1];
			d2p0[2] = inertial_measurements[2];
			w0 [0] = inertial_measurements[3];
			w0 [1] = inertial_measurements[4];
			w0 [2] = inertial_measurements[5];
			dw0 = eval_domega(w0);
		}
		
		b &= getUpperEncodersSpeedAndAcceleration();
		setUpperMeasure(_init);
		b &= getLowerEncodersSpeedAndAcceleration();
		setLowerMeasure(_init);
		return b;
	}

	bool getLowerEncodersSpeedAndAcceleration()
		{
			bool b = true;
			b &= iencs_leg_left->getEncoders(encoders_leg_left.data());
			b &= iencs_leg_right->getEncoders(encoders_leg_right.data());
			b &= iencs_torso->getEncoders(encoders_torso.data());

			for (int i=0;i<q_torso.length();i++)
			{
				q_torso(i) = encoders_torso(2-i);
				all_q_low(i) = q_torso(i);
			}
			for (int i=0;i<q_lleg.length();i++)
			{
				q_lleg(i) = encoders_leg_left(i);
				all_q_low(q_torso.length()+i) = q_lleg(i);
			}
			for (int i=0;i<q_rleg.length();i++)
			{
				q_rleg(i) = encoders_leg_right(i);
				all_q_low(q_torso.length()+q_lleg.length()+i) = q_rleg(i);
			}
			all_dq_low = evalVelLow(all_q_low);
			all_d2q_low = evalAccLow(all_q_low);
			for (int i=0;i<q_torso.length();i++)
			{
				dq_torso(i) = all_dq_low(i);
				d2q_torso(i) = all_d2q_low(i);
			}
			for (int i=0;i<q_lleg.length();i++)
			{
				dq_lleg(i) = all_dq_low(i+q_torso.length());
				d2q_lleg(i) = all_d2q_low(i+q_torso.length());
			}
			for (int i=0;i<q_rleg.length();i++)
			{
				dq_rleg(i) = all_dq_low(i+q_torso.length()+q_lleg.length());
				d2q_rleg(i) = all_d2q_low(i+q_torso.length()+q_lleg.length());
			}

			return b;
		}


	bool getUpperEncodersSpeedAndAcceleration()
	{
		bool b = true;
		b &= iencs_arm_left->getEncoders(encoders_arm_left.data());
		b &= iencs_arm_right->getEncoders(encoders_arm_right.data());
		b &= iencs_head->getEncoders(encoders_head.data());		

		for (int i=0;i<q_head.length();i++)
		{
			q_head(i) = encoders_head(i);
			all_q_up(i) = q_head(i);
		}
		for (int i=0;i<q_larm.length();i++)
		{
			q_larm(i) = encoders_arm_left(i);
			all_q_up(q_head.length()+i) = q_larm(i);
		}
		for (int i=0;i<q_rarm.length();i++)
		{
			q_rarm(i) = encoders_arm_right(i);
			all_q_up(q_head.length()+q_larm.length()+i) = q_rarm(i);
		}
		all_dq_up = evalVelUp(all_q_up);
		all_d2q_up = evalAccUp(all_q_up);
		
		for (int i=0;i<q_head.length();i++)
		{
			dq_head(i) = all_dq_up(i);
			d2q_head(i) = all_d2q_up(i);
		}
		for (int i=0;i<q_larm.length();i++)
		{
			dq_larm(i) = all_dq_up(i+q_head.length());
			d2q_larm(i) = all_d2q_up(i+q_head.length());
		}
		for (int i=0;i<q_rarm.length();i++)
		{
			dq_rarm(i) = all_dq_up(i+q_head.length()+q_larm.length());
			d2q_rarm(i) = all_d2q_up(i+q_head.length()+q_larm.length());
		}

		return b;
	}

	void setLowerMeasure(bool _init=false)
	{
		if(!_init)
		{
			icub.lowerTorso->setAng("torso",CTRL_DEG2RAD * q_torso);
			icub.lowerTorso->setDAng("torso",CTRL_DEG2RAD * dq_torso);
			icub.lowerTorso->setD2Ang("torso",CTRL_DEG2RAD * d2q_torso);

			icub.lowerTorso->setAng("left_leg",CTRL_DEG2RAD * q_lleg);
			icub.lowerTorso->setDAng("left_leg",CTRL_DEG2RAD * dq_lleg);
			icub.lowerTorso->setD2Ang("left_leg",CTRL_DEG2RAD * d2q_lleg);

			icub.lowerTorso->setAng("right_leg",CTRL_DEG2RAD * q_rleg);
			icub.lowerTorso->setDAng("right_leg",CTRL_DEG2RAD * dq_rleg);
			icub.lowerTorso->setD2Ang("right_leg",CTRL_DEG2RAD * d2q_rleg);

			if(test==VOCAB_COMP)
			{
				icub_sens.lowerTorso->setAng("torso",CTRL_DEG2RAD * q_torso);
				icub_sens.lowerTorso->setDAng("torso",CTRL_DEG2RAD * dq_torso);
				icub_sens.lowerTorso->setD2Ang("torso",CTRL_DEG2RAD * d2q_torso);
				icub_sens.lowerTorso->setAng("left_leg",CTRL_DEG2RAD * q_lleg);
				icub_sens.lowerTorso->setDAng("left_leg",CTRL_DEG2RAD * dq_lleg);
				icub_sens.lowerTorso->setD2Ang("left_leg",CTRL_DEG2RAD * d2q_lleg);
				icub_sens.lowerTorso->setAng("right_leg",CTRL_DEG2RAD * q_rleg);
				icub_sens.lowerTorso->setDAng("right_leg",CTRL_DEG2RAD * dq_rleg);
				icub_sens.lowerTorso->setD2Ang("right_leg",CTRL_DEG2RAD * d2q_rleg);
			}
		}
		else
		{
			icub_sens.lowerTorso->setAng("torso",CTRL_DEG2RAD * q_torso);
			icub_sens.lowerTorso->setDAng("torso",CTRL_DEG2RAD * dq_torso);
			icub_sens.lowerTorso->setD2Ang("torso",CTRL_DEG2RAD * d2q_torso);

			icub_sens.lowerTorso->setAng("left_leg",CTRL_DEG2RAD * q_lleg);
			icub_sens.lowerTorso->setDAng("left_leg",CTRL_DEG2RAD * dq_lleg);
			icub_sens.lowerTorso->setD2Ang("left_leg",CTRL_DEG2RAD * d2q_lleg);

			icub_sens.lowerTorso->setAng("right_leg",CTRL_DEG2RAD * q_rleg);
			icub_sens.lowerTorso->setDAng("right_leg",CTRL_DEG2RAD * dq_rleg);
			icub_sens.lowerTorso->setD2Ang("right_leg",CTRL_DEG2RAD * d2q_rleg);
		}
	}

	void setUpperMeasure(bool _init=false)
	{
		if(!_init)
		{
			icub.upperTorso->setAng("head",CTRL_DEG2RAD * q_head);
			icub.upperTorso->setAng("left_arm",CTRL_DEG2RAD * q_larm);
			icub.upperTorso->setAng("right_arm",CTRL_DEG2RAD * q_rarm);
			icub.upperTorso->setDAng("head",CTRL_DEG2RAD * dq_head);
			icub.upperTorso->setDAng("left_arm",CTRL_DEG2RAD * dq_larm);
			icub.upperTorso->setDAng("right_arm",CTRL_DEG2RAD * dq_rarm);
			icub.upperTorso->setD2Ang("head",CTRL_DEG2RAD * d2q_head);
			icub.upperTorso->setD2Ang("left_arm",CTRL_DEG2RAD * d2q_larm);
			icub.upperTorso->setD2Ang("right_arm",CTRL_DEG2RAD * d2q_rarm);
			icub.upperTorso->setInertialMeasure(w0,dw0,d2p0);

			if(test==VOCAB_COMP)
			{
				icub_sens.upperTorso->setAng("head",CTRL_DEG2RAD * q_head);
				icub_sens.upperTorso->setAng("left_arm",CTRL_DEG2RAD * q_larm);
				icub_sens.upperTorso->setAng("right_arm",CTRL_DEG2RAD * q_rarm);
				icub_sens.upperTorso->setDAng("head",CTRL_DEG2RAD * dq_head);
				icub_sens.upperTorso->setDAng("left_arm",CTRL_DEG2RAD * dq_larm);
				icub_sens.upperTorso->setDAng("right_arm",CTRL_DEG2RAD * dq_rarm);
				icub_sens.upperTorso->setD2Ang("head",CTRL_DEG2RAD * d2q_head);
				icub_sens.upperTorso->setD2Ang("left_arm",CTRL_DEG2RAD * d2q_larm);
				icub_sens.upperTorso->setD2Ang("right_arm",CTRL_DEG2RAD * d2q_rarm);
				icub_sens.upperTorso->setInertialMeasure(w0,dw0,d2p0);
			}
		}
		else
		{
			icub_sens.upperTorso->setAng("head",CTRL_DEG2RAD * q_head);
			icub_sens.upperTorso->setAng("left_arm",CTRL_DEG2RAD * q_larm);
			icub_sens.upperTorso->setAng("right_arm",CTRL_DEG2RAD * q_rarm);
			icub_sens.upperTorso->setDAng("head",CTRL_DEG2RAD * dq_head);
			icub_sens.upperTorso->setDAng("left_arm",CTRL_DEG2RAD * dq_larm);
			icub_sens.upperTorso->setDAng("right_arm",CTRL_DEG2RAD * dq_rarm);
			icub_sens.upperTorso->setD2Ang("head",CTRL_DEG2RAD * d2q_head);
			icub_sens.upperTorso->setD2Ang("left_arm",CTRL_DEG2RAD * d2q_larm);
			icub_sens.upperTorso->setD2Ang("right_arm",CTRL_DEG2RAD * d2q_rarm);
			icub_sens.upperTorso->setInertialMeasure(w0,dw0,d2p0);
		}
	}

	void setZeroJntAngVelAcc()
	{
		dq_head = 0.0;
		d2q_head = 0.0;
		dq_larm = 0.0;
		d2q_larm = 0.0;
		dq_rarm = 0.0;
		d2q_rarm = 0.0;
		
		dq_rleg=0.0;
		d2q_rleg=0.0;
		dq_lleg=0.0;
		d2q_lleg=0.0;
		dq_torso=0.0;
		d2q_torso=0.0;
	}
};
// class dataCollector: class for reading from Vrow and providing for FT values on an output port

class dataFilter : public BufferedPort<Bottle>
{
private:
	BufferedPort<Vector> &port_filtered;
	Vector g;
	//Filter *filter;
	
	virtual void onRead(Bottle &b)
	{
		Stamp info;
		BufferedPort<Bottle>::getEnvelope(info);

		size_t sz = b.size();
		Vector x(sz);
		Vector inertial(sz);
		for(unsigned int i=0;i<sz;i++)
		{
			x[i]=b.get(i).asDouble();
			inertial(i)=lpf_ord1_3hz(x(i), i);
		}
		g[0] = inertial[3];
		g[1] = inertial[4];
		g[2] = inertial[5]; 
		g[3] = inertial[6];
		g[4] = inertial[7];
		g[5] = inertial[8];
		//g = (9.81/norm(g))*g;
		
		port_filtered.prepare() = g;
		port_filtered.setEnvelope(info);
		port_filtered.write();
	}
public:
	dataFilter(BufferedPort<Vector> &_port_filtered, ResourceFinder &rf):	
	port_filtered(_port_filtered)
	{
		/*Vector num(3);
		Vector den(3);
		num[0]=0.0030; num[1]=0.0059; num[2]=0.0030;
		den[0]=1.0000;den[1]=-1.8404;den[2]=0.8522;*/
		//		  filter = new Filter(num,den,0.0);
		g.resize(6);
	}
};


class wholeBodyTorqueObserver: public RFModule
{
private:
    Property OptionsLeftArm;
    Property OptionsRightArm;
    Property OptionsHead;
    Property OptionsLeftLeg;
    Property OptionsRightLeg;
    Property OptionsTorso;

	
	dataFilter *port_inertial_input;
	BufferedPort<Vector> port_filtered;	

    inverseDynamics *inv_dyn;

    PolyDriver *dd_left_arm;
    PolyDriver *dd_right_arm;
    PolyDriver *dd_head;
    PolyDriver *dd_left_leg;
    PolyDriver *dd_right_leg;
    PolyDriver *dd_torso;

public:
    wholeBodyTorqueObserver()
    {
        inv_dyn=0;
        dd_left_arm=0;
        dd_right_arm=0;
        dd_head=0;
        dd_left_leg=0;
        dd_right_leg=0;
        dd_torso=0;
    }

    virtual bool createDriver(PolyDriver *_dd)
    {
        // Creating Driver for Limb...
        if (!_dd || !(_dd->isValid()))
        {
            fprintf(stderr,"It is not possible to instantiate the device driver\nreturning...");
            return 0;
        }

        IEncoders *encs;

        bool ok = true;
        ok = ok & _dd->view(encs);
        if (!ok)
        {
            fprintf(stderr,"ERROR: one or more devices has not been viewed\nreturning...");
            return false;
        }

        return true;
    }

    bool configure(ResourceFinder &rf)
    {
		port_filtered.open("/filtered/inertial:o");
		port_inertial_input = new dataFilter(port_filtered, rf);
		port_inertial_input->useCallback();
		port_inertial_input->open("/unfiltered/inertial:i");
		//Time::delay(1.0);
		Network::connect("/icub/inertial","/unfiltered/inertial:i");


		string fwdSlash = "/";
		Bottle tmp;
		int rate = 100;
		tmp=0;

		string name;
		if (rf.check("name"))
			name = rf.find("name").asString();
		else name = "wholeBodyTorqueObserver";


		//---------------------RATE-----------------------------//
		if (rf.check("rate"))
		{
			rate = rf.find("rate").asInt();
			fprintf(stderr,"rateThread working at %d ms\n", rate);
		}
		else
		{
			fprintf(stderr,"Could not find rate in the config file\nusing 100ms as default");
			rate = 100;
		}


		//---------------------DEVICES--------------------------//

		OptionsHead.put("device","remote_controlboard");
		OptionsHead.put("local",(fwdSlash+name+"/head/client").c_str());
		OptionsHead.put("remote","/icub/head");

		dd_head = new PolyDriver(OptionsHead);
		if (!createDriver(dd_head))
		{
			fprintf(stderr,"ERROR: unable to create head device driver...quitting\n");
			return false;
		}
		else
			fprintf(stderr,"device driver created\n");

		OptionsLeftArm.put("device","remote_controlboard");
		OptionsLeftArm.put("local",(fwdSlash+name+"/left_arm/client").c_str());
		OptionsLeftArm.put("remote","/icub/left_arm");
		dd_left_arm = new PolyDriver(OptionsLeftArm);
		if (!createDriver(dd_left_arm))
		{
			fprintf(stderr,"ERROR: unable to create left arm device driver...quitting\n");
			return false;
		}

		OptionsRightArm.put("device","remote_controlboard");
		OptionsRightArm.put("local",(fwdSlash+name+"/right_arm/client").c_str());
		OptionsRightArm.put("remote","/icub/right_arm");
		dd_right_arm = new PolyDriver(OptionsRightArm);
		if (!createDriver(dd_right_arm))
		{
			fprintf(stderr,"ERROR: unable to create right arm device driver...quitting\n");
			return false;
		}

		OptionsLeftLeg.put("device","remote_controlboard");
		OptionsLeftLeg.put("local",(fwdSlash+name+"/left_leg/client").c_str());
		OptionsLeftLeg.put("remote","/icub/left_leg");
		dd_left_leg = new PolyDriver(OptionsLeftLeg);
		if (!createDriver(dd_left_leg))
		{
			fprintf(stderr,"ERROR: unable to create left leg device driver...quitting\n");
			return false;
		}

		OptionsRightLeg.put("device","remote_controlboard");
		OptionsRightLeg.put("local",(fwdSlash+name+"/right_leg/client").c_str());
		OptionsRightLeg.put("remote","/icub/right_leg");
		dd_right_leg = new PolyDriver(OptionsRightLeg);
		if (!createDriver(dd_right_leg))
		{
			fprintf(stderr,"ERROR: unable to create right leg device driver...quitting\n");
			return false;
		}

		OptionsTorso.put("device","remote_controlboard");
		OptionsTorso.put("local",(fwdSlash+name+"/torso/client").c_str());
		OptionsTorso.put("remote","/icub/torso");

		dd_torso = new PolyDriver(OptionsTorso);
		if (!createDriver(dd_torso))
		{
			fprintf(stderr,"ERROR: unable to create head device driver...quitting\n");
			return false;
		}
		else
			fprintf(stderr,"device driver created\n");

		//--------------------------THREAD--------------------------
		inv_dyn = new inverseDynamics(rate, dd_left_arm, dd_right_arm, dd_head, dd_left_leg, dd_right_leg, dd_torso);
		fprintf(stderr,"ft thread istantiated...\n");
		inv_dyn->start();
		fprintf(stderr,"thread started\n");

		return true;
    }

    bool close()
    {
        fprintf(stderr,"closing... \n");     

		if (inv_dyn)
		  {
			fprintf(stderr,"Stopping the inv_dyn module...");     
			inv_dyn->stop();
			fprintf(stderr,"inv_dyn module stopped\n");     
			delete inv_dyn;
			inv_dyn=0;
		  }

		fprintf(stderr,"interrupting the filtered port \n");     
		port_filtered.interrupt();
		fprintf(stderr,"closing the filtered port \n");     
		port_filtered.close();

		if (dd_left_arm)
		{
			fprintf(stderr,"Closing dd_left_arm \n");     
			dd_left_arm->close();
			delete dd_left_arm;
			dd_left_arm=0;
		}
		if (dd_right_arm)
		{
			fprintf(stderr,"Closing dd_right_arm \n");     
			dd_right_arm->close();
			delete dd_right_arm;
			dd_right_arm=0;
		}
		if (dd_head)
		{
			fprintf(stderr,"Closing dd_head \n");     
			dd_head->close();
			delete dd_head;
			dd_head=0;
		}

		if (dd_left_leg)
		{
			fprintf(stderr,"Closing dd_left_leg \n");     
			dd_left_leg->close();
			delete dd_left_leg;
			dd_left_leg=0;
		}
		if (dd_right_leg)
		{
			fprintf(stderr,"Closing dd_right_leg \n");     
			dd_right_leg->close();
			delete dd_right_leg;
			dd_right_leg=0;
		}
		if (dd_torso)
		{
			fprintf(stderr,"Closing dd_torso \n");     
			dd_torso->close();
			delete dd_torso;
			dd_torso=0;
		}

		if(port_inertial_input)
		{
			fprintf(stderr,"interrupting the inertial input port \n");     
			port_inertial_input->interrupt();
			port_inertial_input->close();
			delete port_inertial_input;
			port_inertial_input=0;
		}

		fprintf(stderr,"wholeBodyTorqueObserver module was closed successfully! \n");     
        return true;
    }

    double getPeriod()
	{
		return 1.0;
	}
    bool updateModule() 
	{
	    static unsigned long int alive_counter = 0;
        static double curr_time = Time::now();
        if (Time::now() - curr_time > 60)
        {
            printf ("wholeBodyTorqueObserver is alive! running for %ld mins.\n",++alive_counter);
            curr_time = Time::now();
        }

		if (inv_dyn==0) 
			return false;
		thread_status_enum thread_status = inv_dyn->getThreadStatus();
		if (thread_status==STATUS_OK)
			return true;
		else if (thread_status==STATUS_DISCONNECTED)
		{
			printf ("wholeBodyTorqueObserver module lost connection with iCubInterface, now closing...\n");
			return false;
		}
		else
		{
			fprintf(stderr,"wholeObserver module was closed successfully! \n");    
			return true;
		}
			
	}
};


int main(int argc, char * argv[])
{
    ResourceFinder rf;
    rf.setVerbose(true);
    rf.configure("ICUB_ROOT",argc,argv);

    if (rf.check("help"))
    {
        cout << "Options:" << endl << endl;
        cout << "\t--context context: where to find the called resource (referred to $ICUB_ROOT/app: default wrechObserver/conf)" << endl;
        cout << "\t--from       from: the name of the file.ini to be used for calibration"                                        << endl;
        cout << "\t--rate       rate: the period used by the module. default 100ms (not less than 15ms)"                          << endl;
        return 0;
    }

    Network yarp;

    if (!yarp.checkNetwork())
        return -1;

    wholeBodyTorqueObserver obs;

    return obs.runModule(rf);
}

