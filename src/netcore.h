/*NEXUS IRC session BNC, by Subsentient. This software is public domain.*/

/**Header file for network core.**/

#ifndef __NETCORE_HEADER__
#define __NETCORE_HEADER__

//Structures

//Globals
extern int ServerDescriptor, IRCDescriptor;

//Function prototypes
namespace Net
{
	
	namespace Errors
	{
		class Any
		{
		};
		class IOError : public Any
		{
		};
		class BlockingError : public Any
		{
		public:
			const unsigned BytesSentBeforeBlocking;
			BlockingError(unsigned BytesSent) : BytesSentBeforeBlocking(BytesSent) {}
		};
	}
	bool InitServer(unsigned short PortNum);
	bool Read(int Descriptor, void *OutStream_, unsigned MaxLength, bool IsText);
	void Write(int const ClientDescriptor, const void *InMsg, unsigned WriteSize);
	void ShutdownServer(void);
	bool Connect(const char *InHost, unsigned short PortNum, int *SocketDescriptor_);
	bool AcceptClient(int *const OutDescriptor, char *const OutIPAddr, unsigned IPAddrMaxLen);
	bool Close(const int Descriptor);
}
#endif //__NETCORE_HEADER__
