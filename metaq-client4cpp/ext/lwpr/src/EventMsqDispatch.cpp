/*
 * $Id: EventMsqDispatch.cpp 3 2011-08-19 02:25:45Z  $
 */
#include "EventMsqDispatch.h"
#include <assert.h>

namespace LWPR
{

	//////////////////////////////////////////////////////////////////////////
	event_msq_option::event_msq_option()
		: nMsqId(-1),
		  nThreadCount(10),
		  pHandler(NULL)
	{

	}

	//////////////////////////////////////////////////////////////////////////
	EventMsqDispatch::EventMsqDispatch(const EVENT_MSQ_OPTION_T& opt)
		: m_EventMsqOption(opt),
		  m_bContinue(true)
	{
		assert(m_EventMsqOption.nMsqId >= 0);
		assert(m_EventMsqOption.nThreadCount > 0);
		assert(m_EventMsqOption.pHandler != NULL);
	}

	EventMsqDispatch::~EventMsqDispatch()
	{
	}

	void EventMsqDispatch::Start()
	{
		// 创建线程对象
		for(int i = 0; i < m_EventMsqOption.nThreadCount; i++)
		{
			EventMsqThreadImpl* pThread = new EventMsqThreadImpl(*this);
			m_Threads.push_back(pThread);
		}

		// 启动线程
		for(size_t j = 0; j < m_Threads.size(); j++)
		{
			m_Threads[j]->Start();
		}
	}

	void EventMsqDispatch::Halt()
	{
		m_bContinue = false;
		EVENT_MSQ_MAGIC_MSG_T msg;
		IPCMSQ::SendMsg(m_EventMsqOption.nMsqId, (const char*)&msg, sizeof(msg));
	}

	void EventMsqDispatch::Reap()
	{
		// 回收线程
		for(int i = 0; i < m_EventMsqOption.nThreadCount; i++)
		{
			m_Threads[i]->Join();
			delete m_Threads[i];
		}
	}

	//////////////////////////////////////////////////////////////////////////
	EventMsqThreadImpl::EventMsqThreadImpl(EventMsqDispatch& dispatch)
		: m_EventMsqDispatch(dispatch)
	{
	}

	EventMsqThreadImpl::~EventMsqThreadImpl()
	{
	}

	void EventMsqThreadImpl::Run()
	{
		while(m_EventMsqDispatch.m_bContinue)
		{
			try
			{
				Buffer bufMsg;
				memset(bufMsg.Inout(), 0, bufMsg.Capacity());
				IPCMSQ_RET_TYPE_E nRetMsq = IPCMSQ_RET_OK;

				{
					// 加锁，读取消息队列（进入临界区）
					// 经验证：linux平台不需要加锁，但是其他平台由于线程实现机制的特殊性，不能保证消息接收时原子操作
					Synchronized syn(m_EventMsqDispatch.m_Mutex);

					if(!m_EventMsqDispatch.m_bContinue)
					{
						return;
					}
					else
					{
						nRetMsq = IPCMSQ::ReceiveMsg(m_EventMsqDispatch.m_EventMsqOption.nMsqId, bufMsg);
					}
				}

				if(nRetMsq == IPCMSQ_RET_OK)
				{
					// 检测收到的消息是否是关闭消息
					EVENT_MSQ_MAGIC_MSG_T msg;
					if(bufMsg.Size() == sizeof(msg)
					   && !memcmp(bufMsg.Inout(), &msg, sizeof(msg)))
					{
						return;
					}

					try
					{
						m_EventMsqDispatch.m_EventMsqOption.pHandler->DoReceiveMsqData(bufMsg);
					}
					catch(...)
					{
					}
				}
			}
			catch(const LWPR::Exception& e)
			{
				fprintf(stderr, "%s\n", e.what());
			}
		}
	}
};
