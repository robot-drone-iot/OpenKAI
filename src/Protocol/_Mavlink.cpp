#include "_Mavlink.h"
#include "../Utility/util.h"

namespace kai
{

_Mavlink::_Mavlink()
{
	m_pIO = NULL;
	m_msg.init();

	m_mySystemID = 255;
	m_myComponentID = MAV_COMP_ID_MISSIONPLANNER;
	m_myType = MAV_TYPE_GCS;

	m_devSystemID = 0;
	m_devComponentID = 0;
	m_devType = 0;
}

_Mavlink::~_Mavlink()
{
}

bool _Mavlink::init(void* pKiss)
{
	IF_F(!this->_ThreadBase::init(pKiss));
	Kiss* pK = (Kiss*) pKiss;
	pK->m_pInst = this;

	KISSm(pK,mySystemID);
	KISSm(pK,myComponentID);
	KISSm(pK,myType);

	KISSm(pK,devSystemID);
	KISSm(pK,devComponentID);
	KISSm(pK,devType);

	m_msg.sysid = 0;
	m_msg.compid = 0;
	m_status.packet_rx_drop_count = 0;
	m_vPeer.clear();

	return true;
}

bool _Mavlink::link(void)
{
	IF_F(!this->_ThreadBase::link());
	Kiss* pK = (Kiss*) m_pKiss;

	string iName;

	iName = "";
	F_ERROR_F(pK->v("_IOBase", &iName));
	m_pIO = (_IOBase*) (pK->root()->getChildInstByName(&iName));
	IF_Fl(!m_pIO,"_IOBase not found");

	Kiss** pItr = pK->getChildItr();
	int i=0;
	while (pItr[i])
	{
		Kiss* pP = pItr[i];
		IF_F(i >= MAV_N_PEER);
		i++;

		MAVLINK_PEER mP;
		mP.init();

		iName = "";
		F_ERROR_F(pP->v("_Mavlink", &iName));
		mP.m_pPeer = pK->root()->getChildInstByName(&iName);
		if(!mP.m_pPeer)
		{
			LOG_I("_Mavlink not found: " + iName);
			continue;
		}

		m_vPeer.push_back(mP);
	}
	return true;
}

bool _Mavlink::start(void)
{
	//Start thread
	m_bThreadON = true;
	int retCode = pthread_create(&m_threadID, 0, getUpdateThread, this);
	if (retCode != 0)
	{
		LOG_E(retCode);
		m_bThreadON = false;
		return false;
	}

	return true;
}

void _Mavlink::update(void)
{
	while (m_bThreadON)
	{
		if(!m_pIO)
		{
			this->sleepTime(USEC_1SEC);
			continue;
		}

		if(!m_pIO->isOpen())
		{
			this->sleepTime(USEC_1SEC);
			continue;
		}

		this->autoFPSfrom();

		handleMessages();

		this->autoFPSto();
	}
}

void _Mavlink::writeMessage(mavlink_message_t msg)
{
	NULL_(m_pIO);

	IO_BUF ioB;
	ioB.m_nB = mavlink_msg_to_send_buffer(ioB.m_pB, &msg);

	if(m_pIO->ioType()!=io_webSocket)
	{
		m_pIO->write(ioB);
	}
	else
	{
		_WebSocket* pWS = (_WebSocket*)m_pIO;
		pWS->write(ioB.m_pB, ioB.m_nB, WS_MODE_BIN);
	}
}

void _Mavlink::sendHeartbeat(void)
{
	mavlink_message_t msg;
	mavlink_msg_heartbeat_pack(
			m_mySystemID,
			m_myComponentID,
			&msg,
			m_myType,
			0, 0, 0, MAV_STATE_ACTIVE);

	writeMessage(msg);
	LOG_I("<- heartBeat");
}

void _Mavlink::requestDataStream(uint8_t stream_id, int rate)
{
	mavlink_request_data_stream_t D;
	D.target_system = m_devSystemID;
	D.target_component = m_devComponentID;
	D.req_stream_id = stream_id;
	D.req_message_rate = rate;
	D.start_stop = 1;

	mavlink_message_t msg;
	mavlink_msg_request_data_stream_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- requestDataStream");
}

void _Mavlink::gpsInput(mavlink_gps_input_t& D)
{
	mavlink_message_t msg;
	mavlink_msg_gps_input_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- gpsInput");
}

void _Mavlink::setAttitudeTarget(float* pAtti, float* pRate, float thrust, uint8_t mask)
{
	mavlink_set_attitude_target_t D;

	//pAtti: Roll, Pitch, Yaw
	float pQ[4];
	mavlink_euler_to_quaternion(pAtti[0], pAtti[1], pAtti[2], pQ);

	D.target_system = m_devSystemID;
	D.target_component = m_devComponentID;
	D.q[0] = pQ[0];
	D.q[1] = pQ[1];
	D.q[2] = pQ[2];
	D.q[3] = pQ[3];
	D.body_roll_rate = pRate[0];
	D.body_pitch_rate = pRate[1];
	D.body_yaw_rate = pRate[2];
	D.thrust = thrust;
	D.type_mask = mask;

	mavlink_message_t msg;
	mavlink_msg_set_attitude_target_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- setTargetAttitude: r=" + f2str(pAtti[0]) +
			", p=" + f2str(pAtti[1]) +
			", y=" + f2str(pAtti[2]) +
			", thr=" + f2str(thrust));
}

void _Mavlink::landingTarget(mavlink_landing_target_t& D)
{
	D.time_usec = getTimeUsec();

	mavlink_message_t msg;
	mavlink_msg_landing_target_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- LANDING_TARGET: ANGLE_X:" + f2str(D.angle_x) + " ANGLE_Y:" + f2str(D.angle_y));
}

void _Mavlink::clDoSetMode(int mode)
{
	mavlink_command_long_t D;
	D.target_system = m_mySystemID;
	D.target_component = m_devComponentID;
	D.command = MAV_CMD_DO_SET_MODE;
	D.param1 = mode;

	mavlink_message_t msg;
	mavlink_msg_command_long_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- cmdLongDoSetMode: "+i2str(mode));
}

void _Mavlink::clComponentArmDisarm(bool bArm)
{
	mavlink_command_long_t D;
	D.target_system = m_devSystemID;
	D.target_component = m_devComponentID;
	D.command = MAV_CMD_COMPONENT_ARM_DISARM;
	D.param1 = (bArm)?1:0;

	mavlink_message_t msg;
	mavlink_msg_command_long_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- cmdLongComponentArmDisarm: "+i2str(bArm));
}

void _Mavlink::clDoSetPositionYawThrust(float steer, float thrust)
{
	mavlink_command_long_t D;
	D.target_system = m_devSystemID;
	D.target_component = m_devComponentID;
	D.command = 213; //MAV_CMD_DO_SET_POSITION_YAW_THRUST;
	D.confirmation = 0;
	D.param1 = steer;
	D.param2 = thrust;

	mavlink_message_t msg;
	mavlink_msg_command_long_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- cmdLongDoSetPositionYawTrust");
}

void _Mavlink::clDoSetServo(int iServo, int PWM)
{
	mavlink_command_long_t D;
	D.target_system = m_devSystemID;
	D.target_component = m_devComponentID;
	D.command = MAV_CMD_DO_SET_SERVO;
	D.param1 = iServo;
	D.param2 = (float)PWM;

	mavlink_message_t msg;
	mavlink_msg_command_long_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- cmdLongDoSetServo: servo="+i2str(iServo)
			+ " pwm=" + i2str(PWM));
}

void _Mavlink::distanceSensor(mavlink_distance_sensor_t& D)
{
	/*
	 time_boot_ms: anything (it’s ignored)
	 min_distance: 100 (i.e. 1m)
	 max_distance: 1500 (i.e. 15m)
	 current_distance: depth-from-zed-in-cm
	 type: 0 (ignored)
	 id: 0 (also ignored for now)
	 orientation: 0 (means pointing forward)
	 covariance: 255 (ignored for now)
	 */

	D.id = 0;
	D.time_boot_ms = getTimeBootMs();

	mavlink_message_t msg;
	mavlink_msg_distance_sensor_encode(m_mySystemID, m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- DIST_SENSOR sysID = " + i2str(m_mySystemID) +
			", orient = " + i2str((int)D.orientation) +
			", d = " + i2str((int)D.current_distance) +
			", min = " + i2str((int)D.min_distance) +
			", max = " + i2str((int)D.max_distance));
}

void _Mavlink::visionPositionDelta(uint64_t dTime, vDouble3* pDAngle,
		vDouble3* pDPos, uint8_t confidence)
{

	/*
	 * float angle_delta[3];
	 * Rotation in radians in body frame from previous to current frame
	 * using right-hand coordinate system (0=roll, 1=pitch, 2=yaw)
	 * float position_delta[3];
	 * Change in position in meters from previous to current frame
	 * rotated into body frame (0=forward, 1=right, 2=down)
	 * float confidence; //< normalised confidence value from 0 to 100
	 * */

	mavlink_message_t msg;
	mavlink_vision_position_delta_t D;
	D.time_usec = getTimeUsec();
	D.time_delta_usec = dTime;
	D.angle_delta[0] = (float) pDAngle->x;
	D.angle_delta[1] = (float) pDAngle->y;
	D.angle_delta[2] = (float) pDAngle->z;
	D.position_delta[0] = (float) pDPos->x;
	D.position_delta[1] = (float) pDPos->y;
	D.position_delta[2] = (float) pDPos->z;
	D.confidence = (float) confidence;

	mavlink_msg_vision_position_delta_encode(m_mySystemID,
			m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- VISION_POSITION_DELTA dT=" + i2str(dTime)
			+ ", forward=" + i2str(pDPos->x)
			+ ", right=" + i2str(pDPos->y)
			+ ", down=" + i2str(pDPos->z)
			+ "; roll=" + i2str(pDAngle->x)
			+ ", pitch=" + i2str(pDAngle->y)
			+ ", yaw=" + i2str(pDAngle->z)
			+ ", confidence=" + i2str(D.confidence));
}

void _Mavlink::positionTargetLocalNed(mavlink_position_target_local_ned_t& D)
{
	D.time_boot_ms = getTimeBootMs();

	mavlink_message_t msg;
	mavlink_msg_position_target_local_ned_encode(
			m_mySystemID,
			m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- POS_TARGET_LOCAL_NED x=" + i2str(D.x)
			+ ", y=" + i2str(D.y)
			+ ", z=" + i2str(D.z)
			+ ", vx=" + i2str(D.vx)
			+ ", vy=" + i2str(D.vy)
			+ ", vz=" + i2str(D.vz)
			+ ", afx=" + i2str(D.afx)
			+ ", afy=" + i2str(D.afy)
			+ ", afz=" + i2str(D.afz)
			+ ", yaw=" + i2str(D.yaw)
			+ ", yawRate=" + i2str(D.yaw_rate)
			+ ", cFrame=" + i2str(D.coordinate_frame)
			+ ", typeMask=" + i2str(D.type_mask)
			);
}

void _Mavlink::positionTargetGlobalInt(mavlink_position_target_global_int_t& D)
{
	mavlink_message_t msg;
	mavlink_msg_position_target_global_int_encode(
			m_mySystemID,
			m_myComponentID, &msg, &D);

	writeMessage(msg);
	LOG_I("<- POS_TARGET_GLOBAL_INT lat=" + i2str(D.lat_int)
			+ ", lng=" + i2str(D.lon_int)
			+ ", alt=" + i2str(D.alt)
			+ ", vx=" + i2str(D.vx)
			+ ", vy=" + i2str(D.vy)
			+ ", vz=" + i2str(D.vz)
			+ ", afx=" + i2str(D.afx)
			+ ", afy=" + i2str(D.afy)
			+ ", afz=" + i2str(D.afz)
			+ ", yaw=" + i2str(D.yaw)
			+ ", yawRate=" + i2str(D.yaw_rate)
			+ ", cFrame=" + i2str(D.coordinate_frame)
			+ ", typeMask=" + i2str(D.type_mask)
			);
}

void _Mavlink::rcChannelsOverride(mavlink_rc_channels_override_t& D)
{
	D.target_system = m_devSystemID;
	D.target_component = m_devComponentID;

	mavlink_message_t msg;
	mavlink_msg_rc_channels_override_encode(
			m_mySystemID,
			m_myComponentID,
			&msg, &D);

	writeMessage(msg);
	LOG_I("<- rcChannelsOverride, c1=" + i2str(D.chan1_raw)
			+ ", c2=" + i2str(D.chan2_raw)
			+ ", c3=" + i2str(D.chan3_raw)
			+ ", c4=" + i2str(D.chan4_raw)
			+ ", c5=" + i2str(D.chan5_raw)
			+ ", c6=" + i2str(D.chan6_raw)
			+ ", c7=" + i2str(D.chan7_raw)
			+ ", c8=" + i2str(D.chan8_raw)
			);
}

void _Mavlink::setMode(mavlink_set_mode_t& D)
{
	D.target_system = m_devSystemID;

	mavlink_message_t msg;
	mavlink_msg_set_mode_encode(
			m_mySystemID,
			m_myComponentID,
			&msg, &D);

	writeMessage(msg);
	LOG_I("<- setMode, base_mode=" + i2str(D.base_mode)
			+ ", custom_mode=" + i2str(D.custom_mode));
}

bool _Mavlink::readMessage(mavlink_message_t &msg)
{
	uint8_t	rBuf[MAV_N_BUF];
	static int nRead = 0;
	static int iRead = 0;

	if(nRead == 0)
	{
		nRead = m_pIO->read(rBuf, MAV_N_BUF);
		IF_F(nRead <= 0);
		iRead = 0;
	}

	while(iRead < nRead)
	{
		mavlink_status_t status;
		uint8_t result = mavlink_frame_char(MAVLINK_COMM_0, rBuf[iRead], &msg, &status);
		iRead++;

		if (result == 1)
		{
			//Good message decoded
			m_status = status;
			return true;
		}
		else if (result == 2)
		{
			//Bad CRC
			LOG_I("-> DROPPED PACKETS:" + i2str(status.packet_rx_drop_count));
		}
	}

	nRead = 0;
	return false;
}

void _Mavlink::handleMessages()
{
	mavlink_message_t msg;

	while (readMessage(msg))
	{
		uint64_t tNow = getTimeUsec();
		m_msg.sysid = msg.sysid;
		m_msg.compid = msg.compid;

		switch (msg.msgid)
		{

		case MAVLINK_MSG_ID_HEARTBEAT:
		{
			mavlink_msg_heartbeat_decode(&msg, &m_msg.heartbeat);
			m_msg.time_stamps.heartbeat = tNow;

			m_devSystemID = m_msg.sysid;
			m_devComponentID = m_msg.compid;
			m_devType = m_msg.heartbeat.type;

			LOG_I("-> MAVLINK_MSG_ID_HEARTBEAT");
			break;
		}

		case MAVLINK_MSG_ID_SYS_STATUS:
		{
			mavlink_msg_sys_status_decode(&msg, &m_msg.sys_status);
			m_msg.time_stamps.sys_status = tNow;
			LOG_I("-> MAVLINK_MSG_ID_SYS_STATUS");
			break;
		}

		case MAVLINK_MSG_ID_BATTERY_STATUS:
		{
			mavlink_msg_battery_status_decode(&msg, &m_msg.battery_status);
			m_msg.time_stamps.battery_status = tNow;
			LOG_I("-> MAVLINK_MSG_ID_BATTERY_STATUS");
			break;
		}

		case MAVLINK_MSG_ID_RADIO_STATUS:
		{
			mavlink_msg_radio_status_decode(&msg, &m_msg.radio_status);
			m_msg.time_stamps.radio_status = tNow;
			LOG_I("-> MAVLINK_MSG_ID_RADIO_STATUS");
			break;
		}

		case MAVLINK_MSG_ID_LOCAL_POSITION_NED:
		{
			mavlink_msg_local_position_ned_decode(&msg, &m_msg.local_position_ned);
			m_msg.time_stamps.local_position_ned = tNow;
			LOG_I("-> MAVLINK_MSG_ID_LOCAL_POSITION_NED");
			break;
		}

		case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
		{
			mavlink_msg_global_position_int_decode(&msg, &m_msg.global_position_int);
			m_msg.time_stamps.global_position_int = tNow;
			LOG_I("-> MAVLINK_MSG_ID_GLOBAL_POSITION_INT");
			break;
		}

		case MAVLINK_MSG_ID_POSITION_TARGET_LOCAL_NED:
		{
			mavlink_msg_position_target_local_ned_decode(&msg, &m_msg.position_target_local_ned);
			m_msg.time_stamps.position_target_local_ned = tNow;
			LOG_I("-> MAVLINK_MSG_ID_POSITION_TARGET_LOCAL_NED");
			break;
		}

		case MAVLINK_MSG_ID_POSITION_TARGET_GLOBAL_INT:
		{
			mavlink_msg_position_target_global_int_decode(&msg, &m_msg.position_target_global_int);
			m_msg.time_stamps.position_target_global_int = tNow;
			LOG_I("-> MAVLINK_MSG_ID_POSITION_TARGET_GLOBAL_INT");
			break;
		}

		case MAVLINK_MSG_ID_HIGHRES_IMU:
		{
			mavlink_msg_highres_imu_decode(&msg, &m_msg.highres_imu);
			m_msg.time_stamps.highres_imu = tNow;
			LOG_I("-> MAVLINK_MSG_ID_HIGHRES_IMU");
			break;
		}

		case MAVLINK_MSG_ID_ATTITUDE:
		{
			mavlink_msg_attitude_decode(&msg, &m_msg.attitude);
			m_msg.time_stamps.attitude = tNow;
			LOG_I("-> MAVLINK_MSG_ID_ATTITUDE");
			break;
		}

		case MAVLINK_MSG_ID_COMMAND_ACK:
		{
			mavlink_msg_command_ack_decode(&msg, &m_msg.command_ack);
			m_msg.time_stamps.attitude = tNow;
			LOG_I("-> MAVLINK_MSG_ID_COMMAND_ACK: " + i2str(m_msg.command_ack.result));
			break;
		}

		case MAVLINK_MSG_ID_RC_CHANNELS_OVERRIDE:
		{
			mavlink_msg_rc_channels_override_decode(&msg, &(m_msg.rc_channels_override));
			m_msg.time_stamps.rc_channels_override = tNow;

			LOG_I("-> RC_OVERRIDE: chan1=" + i2str(m_msg.rc_channels_override.chan1_raw)
					+ ", chan2=" + i2str(m_msg.rc_channels_override.chan2_raw)
					+ ", chan3=" + i2str(m_msg.rc_channels_override.chan3_raw)
					+ ", chan4=" + i2str(m_msg.rc_channels_override.chan4_raw)
					+ ", chan5=" + i2str(m_msg.rc_channels_override.chan5_raw)
					+ ", chan6=" + i2str(m_msg.rc_channels_override.chan6_raw)
					+ ", chan7=" + i2str(m_msg.rc_channels_override.chan7_raw)
					+ ", chan8=" + i2str(m_msg.rc_channels_override.chan8_raw)
					);
			break;
		}

		case MAVLINK_MSG_ID_RC_CHANNELS_RAW:
		{
			mavlink_msg_rc_channels_raw_decode(&msg, &(m_msg.rc_channels_raw));
			m_msg.time_stamps.rc_channels_raw = tNow;

			LOG_I("-> RC_RAW: chan1=" + i2str(m_msg.rc_channels_raw.chan1_raw)
					+ ", chan2=" + i2str(m_msg.rc_channels_raw.chan2_raw)
					+ ", chan3=" + i2str(m_msg.rc_channels_raw.chan3_raw)
					+ ", chan4=" + i2str(m_msg.rc_channels_raw.chan4_raw)
					+ ", chan5=" + i2str(m_msg.rc_channels_raw.chan5_raw)
					+ ", chan6=" + i2str(m_msg.rc_channels_raw.chan6_raw)
					+ ", chan7=" + i2str(m_msg.rc_channels_raw.chan7_raw)
					+ ", chan8=" + i2str(m_msg.rc_channels_raw.chan8_raw)
					);
			break;
		}

		default:
		{
			LOG_I("-> UNKNOWN MSG_ID:" + i2str(msg.msgid));
			break;
		}

		}

		//Message routing
		for(int i=0; i<m_vPeer.size(); i++)
		{
			MAVLINK_PEER* pMP = &m_vPeer[i];
			IF_CONT(!pMP->bCmdRoute(msg.msgid));

			_Mavlink* pM = (_Mavlink*)pMP->m_pPeer;
			IF_CONT(!pM);
			pM->writeMessage(msg);
		}
	}
}

void _Mavlink::setCmdRoute(uint32_t iCmd, bool bON)
{
	for(int i=0; i<m_vPeer.size(); i++)
	{
		m_vPeer[i].setCmdRoute(iCmd, bON);
	}
}

bool _Mavlink::draw(void)
{
	IF_F(!this->_ThreadBase::draw());
	Window* pWin = (Window*) this->m_pWindow;

	string msg;

	if (!m_pIO->isOpen())
	{
		pWin->tabNext();
		msg = "Not Connected";
		pWin->addMsg(&msg);
		pWin->tabPrev();
		return true;
	}

	pWin->tabNext();

	msg = "y=" + f2str((double)m_msg.attitude.yaw) +
			" p=" + f2str((double)m_msg.attitude.pitch) +
			" r=" + f2str((double)m_msg.attitude.roll);
	pWin->addMsg(&msg);

	msg = "hdg=" + f2str(((double)m_msg.global_position_int.hdg)*0.01);
	pWin->addMsg(&msg);

	msg = "height=" + f2str(((double)m_msg.global_position_int.alt)*0.001);
	pWin->addMsg(&msg);

	pWin->tabPrev();

	return true;
}

bool _Mavlink::cli(int& iY)
{
	IF_F(!this->_ThreadBase::cli(iY));

	string msg;

	msg = "mySysID=" + i2str(m_mySystemID)
			+ " myComID=" + i2str(m_myComponentID)
			+ " myType=" + i2str(m_myType);
	COL_MSG;
	iY++;
	mvaddstr(iY, CLI_X_MSG, msg.c_str());

	msg = "devSysID=" + i2str(m_devSystemID)
			+ " devComID=" + i2str(m_devComponentID)
			+ " devType=" + i2str(m_devType)
	 	 	+ " custom_mode=" + i2str((int)m_msg.heartbeat.custom_mode);

	COL_MSG;
	iY++;
	mvaddstr(iY, CLI_X_MSG, msg.c_str());

	return true;
}

}
