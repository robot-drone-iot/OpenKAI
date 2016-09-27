

#ifndef DETECTOR_CASCADEDETECTOR_H_
#define DETECTOR_CASCADEDETECTOR_H_

#include "../Base/common.h"
#include "../Base/cvplatform.h"
#include "DetectorBase.h"
#include "../Utility/util.h"
#include "../Stream/_Stream.h"

using namespace cv;
using namespace cv::cuda;
using namespace std;

#define CASCADE_CPU 0
#define CASCADE_CUDA 1

namespace kai
{

struct CASCADE_OBJECT
{
	uint16_t		m_status;
	uint64_t		m_frameID;
	Rect			m_boundBox;
};

class _Cascade: public DetectorBase, public _ThreadBase
{
public:
	_Cascade();
	~_Cascade();

	bool init(Config* pConfig);
	bool link(void);
	bool start(void);

	int  getObjList(CASCADE_OBJECT** ppObj);

private:
	inline int findVacancy(int iStart);
	inline void deleteOutdated(void);
	void detect(void);
	void detectCUDA(void);
	void update(void);
	static void* getUpdateThread(void* This)
	{
		((_Cascade*) This)->update();
		return NULL;
	}

public:
	int					m_device;
	Ptr<cuda::CascadeClassifier> m_pCascade;
	cv::CascadeClassifier	m_CC;

	int 				m_iObj;
	int					m_numObj;
	CASCADE_OBJECT* 	m_pObj;
	uint64_t			m_frameID;
	uint64_t			m_objLifeTime;
	int					m_posDiff;

	_Stream*			m_pCamStream;
	int					m_cudaDeviceID;

private:
	Mat			m_Mat;
	GpuMat		m_GMat;
	Frame*	m_pGray;





};
}

#endif /* SRC_FASTDETECTOR_H_ */
