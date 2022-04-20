#pragma once

class RingBuffer
{
	enum
	{
		BUFFER_SIZE = 10000
	};
public:
	RingBuffer();
	RingBuffer(int iBufferSize);
	~RingBuffer();

	int		Enqueue(const char* ptr, int iSize);
	int		Dequeue(char* ptr, int iSize);
	int		Peek(char* ptr, int iSize);

	int		DirectEnqueueSize(void);
	int		DirectDequeueSize(void);

	void	MoveRear(int iSize);
	void	MoveFront(int iSize);

	void	ClearBuffer(void);

	inline char* GetBufferPtr(void) { return &_buffer[0]; }
	inline char* GetFrontBufferPtr(void) { return &_buffer[_iFront + 1]; }
	inline char* GetRearBufferPtr(void) { return &_buffer[_iRear + 1]; }

	inline int	 GetBufferSize() { return _iBufferSize - 1; }
	int		GetUseSize();
	int		GetFreeSize() const;
	void	ReSize(int iSize);

private:
	char* _buffer;
	int		_iFront;
	int		_iRear;
	int		_iBufferSize;
};

