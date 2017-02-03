/*
 * _Lightware_SF40.h
 *
 *  Created on: Nov 4, 2016
 *      Author: yankai
 */

#ifndef AI__Lightware_SF40_H_
#define AI__Lightware_SF40_H_

#include "../Base/common.h"
#include "../Sensor/_Lightware_SF40_sender.h"
#include "../IO/SerialPort.h"
#include "../IO/File.h"
#include "../IO/TCP.h"
#include "../Algorithm/Filter.h"

#define DEG_AROUND 360.0

#define SF40_UART 0
#define SF40_FILE 1
#define SF40_NET 2

namespace kai
{

using std::string;
using namespace std;
using namespace cv;

class _Lightware_SF40: public _ThreadBase
{
public:
	_Lightware_SF40();
	~_Lightware_SF40();

	bool init(void* pKiss);
	bool link(void);
	bool start(void);
	bool draw(void);

	void setHeading(double hdg);
	vDouble2* getDiffPos(void);
	void reset(void);

private:
	bool connect(void);
	bool updateLidar(void);
	void updatePosition(void);
	bool readLine(void);

	void update(void);
	static void* getUpdateThread(void* This)
	{
		((_Lightware_SF40*) This)->update();
		return NULL;
	}

public:
	IO* m_pIn;
	IO* m_pOut;
	_Lightware_SF40_sender* m_pSF40sender;

	double	m_hdg;
	double	m_offsetAngle;
	int		m_nDiv;
	double	m_dAngle;
	double* m_pDist;
	double	m_minDist;
	double	m_maxDist;
	uint8_t m_MBS;
	uint32_t m_iLine;
	string m_strRecv;
	double m_showScale;

	Filter* m_pX;
	Filter* m_pY;
	vDouble2 m_dPos;
	vDouble2 m_lastPos;
};

}

#endif
