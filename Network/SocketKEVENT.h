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


#ifndef MAC_SOCKET_H
#define MAC_SOCKET_H

#include "SocketDefines.h"

class Socket : public BaseSocket
{
public:
	/** Constructor
	 * @param fd File descriptor to use with this socket
	 * @param readbuffersize Incoming data buffer size
	 * @param writebuffersize Outgoing data buffer size
	 * @param peer Connection
	 */
	Socket(SOCKET fd, size_t readbuffersize, size_t writebuffersize);
    
	/** Destructor
	 */
	virtual ~Socket();
    
	/** Returns the socket's file descriptor
	 */
	SOCKET GetFd() const        { return m_fd; }
    
	/** Locks the socket's write buffer so you can begin a write operation
	 */
	void BurstBegin()   { m_writeMutex.lock(); }
    
	/** Unlocks the socket's write buffer so others can write to it
	 */
	void BurstEnd()     { m_writeMutex.unlock(); }
    
	/** Writes the specified data to the end of the socket's write buffer
	 */
	bool BurstSend(const void * data, size_t bytes);
    
	/** Burst system - Pushes event to queue - do at the end of write events.
	 */
	void BurstPush();
    
	/** Disconnects the socket, removing it from the socket engine, and queues
	 * deletion.
	 */
	void Disconnect();
    
	/** Queues the socket for deletion, and disconnects it, if it is connected
	 */
	void Delete();
    
	/** Implemented ReadCallback()
	 */
	void ReadCallback(size_t len);
    
	/** Implemented WriteCallback()
	 */
	void WriteCallback(size_t len);

    /* */
	void Accept(const sockaddr_in * peer);
    
	/** Get IP in numerical form
	 */
	const char * GetIP() { return inet_ntoa(m_peer.sin_addr); }
    
	/** Are we writable?
	 */
	bool Writable() const;
    
	/** Occurs on error
	 */
	void OnError(int errcode);
	
	/** If for some reason we need to access the buffers directly 
	 */
	INLINE CircularBuffer & GetReadBuffer()		{ return m_readBuffer; }
	INLINE CircularBuffer & GetWriteBuffer()	{ return m_writeBuffer; }	
	
	// Posts a kevent with the specifed arguments.
	void PostEvent(int events);
    
	// Atomic wrapper functions for increasing read/write locks
	void IncSendLock()
    {
        ++m_writeLock;
    }
    
	void DecSendLock()
    {
        --m_writeLock;
    }
    
	bool AcquireSendLock()
	{
		if(m_writeLock)
        {
			return false;
        }
		else
		{
			++m_writeLock;
			return true;
		}
	}
    
	// Get the client's ip in numerical form.
	std::string GetRemoteIP();
    
	/** Are we connected?
     */
	bool IsConnected() const    { return m_connected; }
	bool IsDeleted() const      { return m_deleted; }
    
protected:
    /** This socket's file descriptor
	 */
	SOCKET			m_fd;
    
	/** deleted/disconnected markers
	 */
	std::atomic<bool>	m_deleted;
	std::atomic<bool>	m_connected;
    
	/** Called when connection is opened.
	 */ 
	void _OnConnect();
    
	/** Connected peer
	 */
	sockaddr_in         m_peer;
    
	/** Read (inbound)/Write (outbound) buffer
	 */
	CircularBuffer      m_readBuffer;
	CircularBuffer      m_writeBuffer;
    
	/** Socket's write buffer protection
	 */
    std::mutex          m_writeMutex;
	
	/** Write lock, stops multiple write events from being posted.
	 */ 
	std::atomic<long> 	m_writeLock;
};

#endif
