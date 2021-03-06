/*
 * Game server
 * Copyright (C) 2010 Miroslav 'Wayland' Kudrnac
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
			
#include "Network.h"

#ifdef CONFIG_USE_KEVENT

initialiseSingleton(SocketMgr);

SocketMgr::SocketMgr()
{
    m_kq_fd = kqueue();
    if(m_kq_fd == -1)
    {
        Log.Error(__FUNCTION__, "Could not create kqueue fd.");
        exit(EXIT_FAILURE);
    }
}

SocketMgr::~SocketMgr()
{
    close(m_kq_fd);
}

void SocketMgr::AddSocket(BaseSocket * pSocket, bool listenSocket)
{
	//add socket to storage
	LockingPtr<SocketSet> pSockets(m_sockets, m_socketLock);
	if(pSockets->find(pSocket) == pSockets->end())
	{
		pSockets->insert(pSocket);

		//Add kevent event based on socket activity.
		struct kevent ev;
		memset(&ev, 0, sizeof(ev));

		if(listenSocket)
		{
			EV_SET(&ev, pSocket->GetFd(), EVFILT_READ, EV_ADD, 0, SOMAXCONN, pSocket);
		}
		else
		{
			if(pSocket->Writable())
			{
				EV_SET(&ev, pSocket->GetFd(), EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, pSocket);
			}
			else
			{
				EV_SET(&ev, pSocket->GetFd(), EVFILT_READ, EV_ADD, 0, 0, pSocket);
			}
		}

		if(kevent(m_kq_fd, &ev, 1, NULL, 0, NULL) < 0)
		{
			Log.Warning(__FUNCTION__, "Could not add kevent set on fd %u", pSocket->GetFd());
		}
	}
}

void SocketMgr::RemoveSocket(BaseSocket * pSocket)
{
	//remove socket from storage
	LockingPtr<SocketSet> pSockets(m_sockets, m_socketLock);
	SocketSet::iterator itr = pSockets->find(pSocket);
	if(itr != pSockets->end())
	{
		pSockets->erase(itr);
        
		//Remove from kevent list.
        struct kevent ev, ev2;
        memset(&ev, 0, sizeof(ev));
        memset(&ev2, 0, sizeof(ev2));
        EV_SET(&ev, pSocket->GetFd(), EVFILT_READ, EV_DELETE, 0, 0, NULL);
        EV_SET(&ev2, pSocket->GetFd(), EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        if((kevent(m_kq_fd, &ev, 1, NULL, 0, NULL) < 0) && (kevent(m_kq_fd, &ev2, 1, NULL, 0, NULL) < 0))
        {
            Log.Warning(__FUNCTION__, "Could not remove fd %u from epoll set, errno %u", pSocket->GetFd(), errno);
        }
	}
}

void SocketMgr::CloseAll()
{
	LockingPtr<SocketSet> pSockets(m_sockets, NULL);
	std::list<BaseSocket*> tokill;
	
	m_socketLock.lock();
	for(SocketSet::iterator itr = pSockets->begin();itr != pSockets->end();++itr)
	{
		tokill.push_back(*itr);
	}
	m_socketLock.unlock();
	
	for(std::list<BaseSocket*>::iterator itr = tokill.begin(); itr != tokill.end(); ++itr)
	{
		(*itr)->Disconnect();
	}
	
	size_t size = 0;
	do
	{
		m_socketLock.lock();
		size = pSockets->size();
		m_socketLock.unlock();
	}while(size);
}

void SocketMgr::SpawnWorkerThreads()
{
    ThreadPool.ExecuteTask(new SocketWorkerThread());
}

bool SocketWorkerThread::run()
{
    CommonFunctions::SetThreadName("SocketWorker thread");
    //
    timespec rTimeout;
    rTimeout.tv_sec = 10;
    rTimeout.tv_nsec = 0;
    Socket *pSocket;
    int fd_count;
    int kq_fd = sSocketMgr.GetKqFd();
    int i;
    
    while(m_threadRunning)
    {
        fd_count = kevent(kq_fd, 0, 0, m_rEvents, MAX_EVENTS, &rTimeout);
        for(i = 0;i < fd_count;++i)
        {
            pSocket = static_cast<Socket*>(m_rEvents[i].udata);
            if(pSocket == NULL)
            {
                Log.Error(__FUNCTION__, "kevent returned invalid fd:%lu", m_rEvents[i].ident);
                continue;
            }
            
            if(m_rEvents[i].flags & EV_EOF)
            {
                Log.Error(__FUNCTION__, "EV_EOF flags:%u fflags:%u", m_rEvents[i].flags, m_rEvents[i].fflags);
                pSocket->OnError(errno);
                continue;
            }
            
            if(m_rEvents[i].flags & EV_ERROR)
            {
                Log.Error(__FUNCTION__, "EV_ERROR flags:%u fflags:%u", m_rEvents[i].flags, m_rEvents[i].fflags);
                pSocket->OnError(errno);
                continue;
            }
            
            switch(m_rEvents[i].filter)
            {
                case EVFILT_READ:
                {
                    /* Len is unknown at this point. */
                    pSocket->ReadCallback(0);

                }break;
                case EVFILT_WRITE:
                {                    
                    pSocket->BurstBegin();                          // Lock receive mutex
                    pSocket->WriteCallback(0);                      // Perform actual send()
                    if(pSocket->IsConnected())
                    {
                        if(pSocket->Writable())
                        {
                            pSocket->PostEvent(EVFILT_WRITE);       // Still remaining data.
                        }
                        else
                        {
                            pSocket->DecSendLock();
                        }
                    }
                    pSocket->BurstEnd();                            // Unlock
                }break;
            }
        }
    }
    
    return true;
}

#endif
