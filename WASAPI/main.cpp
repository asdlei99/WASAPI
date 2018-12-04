// ����������WAS�ɼ���Ƶ��demo
/*
References:
https://msdn.microsoft.com/en-us/library/dd370800(v=vs.85).aspx
http://blog.csdn.net/leave_rainbow/article/details/50917043
http://blog.csdn.net/lwsas1/article/details/46862195?locationNum=1
WindowsSDK7-Samples-master\multimedia\audio\CaptureSharedEventDriven
*/
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <iostream>
using namespace std;

// ns(nanosecond) : ���룬ʱ�䵥λ��һ���ʮ�ڷ�֮һ
// 1��=1000����; 1����=1000΢��; 1΢��=1000����

// The REFERENCE_TIME data type defines the units for reference times in DirectShow. 
// Each unit of reference time is 100 nanoseconds.(100����Ϊһ��REFERENCE_TIMEʱ�䵥λ)

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC       (10000000)
#define REFTIMES_PER_MILLISEC  (10000)

#define EXIT_ON_ERROR(hres)  \
	if (FAILED(hres)) { goto Exit; }

#define SAFE_RELEASE(punk)  \
	if ((punk) != NULL)  \
				{ (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID   IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID   IID_IAudioClient = __uuidof(IAudioClient);
const IID   IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

#define MoveMemory RtlMoveMemory
#define CopyMemory RtlCopyMemory
#define FillMemory RtlFillMemory
#define ZeroMemory RtlZeroMemory

#define min(a,b)            (((a) < (b)) ? (a) : (b))

//
//  WAV file writer.
//
//  This is a VERY simple .WAV file writer.
//

//
//  A wave file consists of:
//
//  RIFF header:    8 bytes consisting of the signature "RIFF" followed by a 4 byte file length.
//  WAVE header:    4 bytes consisting of the signature "WAVE".
//  fmt header:     4 bytes consisting of the signature "fmt " followed by a WAVEFORMATEX 
//  WAVEFORMAT:     <n> bytes containing a waveformat structure.
//  DATA header:    8 bytes consisting of the signature "data" followed by a 4 byte file length.
//  wave data:      <m> bytes containing wave data.
//
//
//  Header for a WAV file - we define a structure describing the first few fields in the header for convenience.
//
struct WAVEHEADER
{
	DWORD   dwRiff;                     // "RIFF"
	DWORD   dwSize;                     // Size
	DWORD   dwWave;                     // "WAVE"
	DWORD   dwFmt;                      // "fmt "
	DWORD   dwFmtSize;                  // Wave Format Size
};

//  Static RIFF header, we'll append the format to it.
const BYTE WaveHeader[] =
{
	'R',   'I',   'F',   'F',  0x00,  0x00,  0x00,  0x00, 'W',   'A',   'V',   'E',   'f',   'm',   't',   ' ', 0x00, 0x00, 0x00, 0x00
};

//  Static wave DATA tag.
const BYTE WaveData[] = { 'd', 'a', 't', 'a' };

//
//  Write the contents of a WAV file.  We take as input the data to write and the format of that data.
//
bool WriteWaveFile(HANDLE FileHandle, const BYTE *Buffer, const size_t BufferSize, const WAVEFORMATEX *WaveFormat)
{
	DWORD waveFileSize = sizeof(WAVEHEADER) + sizeof(WAVEFORMATEX) + WaveFormat->cbSize + sizeof(WaveData) + sizeof(DWORD) + static_cast<DWORD>(BufferSize);
	BYTE *waveFileData = new (std::nothrow) BYTE[waveFileSize];
	BYTE *waveFilePointer = waveFileData;
	WAVEHEADER *waveHeader = reinterpret_cast<WAVEHEADER *>(waveFileData);

	if (waveFileData == NULL)
	{
		printf("Unable to allocate %d bytes to hold output wave data\n", waveFileSize);
		return false;
	}

	//
	//  Copy in the wave header - we'll fix up the lengths later.
	//
	CopyMemory(waveFilePointer, WaveHeader, sizeof(WaveHeader));
	waveFilePointer += sizeof(WaveHeader);

	//
	//  Update the sizes in the header.
	//
	waveHeader->dwSize = waveFileSize - (2 * sizeof(DWORD));
	waveHeader->dwFmtSize = sizeof(WAVEFORMATEX) + WaveFormat->cbSize;

	//
	//  Next copy in the WaveFormatex structure.
	//
	CopyMemory(waveFilePointer, WaveFormat, sizeof(WAVEFORMATEX) + WaveFormat->cbSize);
	waveFilePointer += sizeof(WAVEFORMATEX) + WaveFormat->cbSize;


	//
	//  Then the data header.
	//
	CopyMemory(waveFilePointer, WaveData, sizeof(WaveData));
	waveFilePointer += sizeof(WaveData);
	*(reinterpret_cast<DWORD *>(waveFilePointer)) = static_cast<DWORD>(BufferSize);
	waveFilePointer += sizeof(DWORD);

	//
	//  And finally copy in the audio data.
	//
	CopyMemory(waveFilePointer, Buffer, BufferSize);

	//
	//  Last but not least, write the data to the file.
	//
	DWORD bytesWritten;
	if (!WriteFile(FileHandle, waveFileData, waveFileSize, &bytesWritten, NULL))
	{
		printf("Unable to write wave file: %d\n", GetLastError());
		delete[]waveFileData;
		return false;
	}

	if (bytesWritten != waveFileSize)
	{
		printf("Failed to write entire wave file\n");
		delete[]waveFileData;
		return false;
	}
	delete[]waveFileData;
	return true;
}

//
//  Write the captured wave data to an output file so that it can be examined later.
//
void SaveWaveData(BYTE *CaptureBuffer, size_t BufferSize, const WAVEFORMATEX *WaveFormat)
{
	HRESULT hr = NOERROR;

	SYSTEMTIME st;
	GetLocalTime(&st);
	char waveFileName[_MAX_PATH] = { 0 };
	sprintf_s(waveFileName, ".\\WAS_%04d-%02d-%02d_%02d_%02d_%02d_%02d.wav",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	HANDLE waveHandle = CreateFile(waveFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	if (waveHandle != INVALID_HANDLE_VALUE)
	{
		if (WriteWaveFile(waveHandle, CaptureBuffer, BufferSize, WaveFormat))
		{
			printf("Successfully wrote WAVE data to %s\n", waveFileName);
		}
		else
		{
			printf("Unable to write wave file\n");
		}
		CloseHandle(waveHandle);
	}
	else
	{
		printf("Unable to open output WAV file %s: %d\n", waveFileName, GetLastError());
	}

}

#define DEF_CAPTURE_MIC
/*
ע1: ����ʱ ���0
ע2: ����ʱ Ӧ�ý�¼���豸�е���˷���ΪĬ���豸
ע3: ����DEF_CAPTURE_MICʱ�����Բɼ���˷� ������Բɼ�������
ע4:
���Բɼ�����:
Initializeʱ��Ҫ����AUDCLNT_STREAMFLAGS_LOOPBACK
����ģʽ�£���Ƶengine�Ὣrending�豸���ڲ��ŵ���Ƶ���� ����һ�ݵ���Ƶ��endpoint buffer
�����Ļ���WASAPI client���Բɼ���the stream.
��ʱ���ɼ���Speaker������
*/

int main(int argc, char* argv[])
{
	HRESULT hr;

	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice           *pDevice = NULL;
	IAudioClient        *pAudioClient = NULL;
	IAudioCaptureClient *pCaptureClient = NULL;
	WAVEFORMATEX        *pwfx = NULL;

	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	UINT32         bufferFrameCount;
	UINT32         numFramesAvailable;

	BYTE           *pData;
	UINT32         packetLength = 0;
	DWORD          flags;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		printf("Unable to initialize COM in thread: %x\n", hr);
		return hr;
	}

	// ����ö�������Ƶ�豸
	// ����������ʱ���ȡ������������п��õ��豸����ָ������Ҫ�õ����Ǹ�����
	hr = CoCreateInstance(CLSID_MMDeviceEnumerator,
		NULL,
		CLSCTX_ALL,
		IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_ERROR(hr)

#ifdef DEF_CAPTURE_MIC
	hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice); // �ɼ���˷�
	//hr = pEnumerator->GetDefaultAudioEndpoint(eCapture,  eMultimedia, &pDevice);
#else 
	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);  // �ɼ�����
#endif	

	EXIT_ON_ERROR(hr)

	// ����һ���������ͨ�������Ի�ȡ������Ҫ��һ������
	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->GetMixFormat(&pwfx);
	EXIT_ON_ERROR(hr)

		/*
		typedef struct tWAVEFORMATEX
		{
		WORD        wFormatTag;         // format type
		WORD        nChannels;          // number of channels (i.e. mono, stereo...)
		DWORD       nSamplesPerSec;     // sample rate
		DWORD       nAvgBytesPerSec;    // for buffer estimation
		WORD        nBlockAlign;        // block size of data
		WORD        wBitsPerSample;     // number of bits per sample of mono data
		WORD        cbSize;             // the count in bytes of the size of  extra information (after cbSize)
		} WAVEFORMATEX;
		*/
		printf("\nGetMixFormat...\n");
	cout << "wFormatTag      : " << pwfx->wFormatTag << endl
		<< "nChannels       : " << pwfx->nChannels << endl
		<< "nSamplesPerSec  : " << pwfx->nSamplesPerSec << endl
		<< "nAvgBytesPerSec : " << pwfx->nAvgBytesPerSec << endl
		<< "nBlockAlign     : " << pwfx->nBlockAlign << endl
		<< "wBitsPerSample  : " << pwfx->wBitsPerSample << endl
		<< "cbSize          : " << pwfx->cbSize << endl << endl;

	// test for IsFormatSupported
	//////////////////////////////////////////////////////////////////////////
	/*WAVEFORMATEX *wf;
	hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, pwfx, &wf);
	if (FAILED(hr))
	{
	printf("IsFormatSupported fail.\n");
	}
	printf("\IsFormatSupported...\n");
	cout<<"wFormatTag      : "<<wf->wFormatTag<<endl
	<<"nChannels       : "<<wf->nChannels<<endl
	<<"nSamplesPerSec  : "<<wf->nSamplesPerSec<<endl
	<<"nAvgBytesPerSec : "<<wf->nAvgBytesPerSec<<endl
	<<"nBlockAlign     : "<<wf->nBlockAlign<<endl
	<<"wBitsPerSample  : "<<wf->wBitsPerSample<<endl
	<<"cbSize          : "<<wf->cbSize<<endl<<endl;*/
	//////////////////////////////////////////////////////////////////////////

	int nFrameSize = (pwfx->wBitsPerSample / 8) * pwfx->nChannels;

	cout << "nFrameSize           : " << nFrameSize << " Bytes" << endl
		<< "hnsRequestedDuration : " << hnsRequestedDuration
		<< " REFERENCE_TIME time units. ��(" << hnsRequestedDuration / 10000 << "ms)" << endl;

	// ��ʼ�������������������ָ��������󻺳������ȣ��������Ҫ��
	// Ӧ�ó���������ݿ�Ĵ�С�Լ���ʱ���̶�������ĳ�ʼ�������������ҿ����ĵ�����
	// https://msdn.microsoft.com/en-us/library/dd370875(v=vs.85).aspx
#ifdef DEF_CAPTURE_MIC
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
		hnsRequestedDuration,
		0,
		pwfx,
		NULL);
#else
	/*
	The AUDCLNT_STREAMFLAGS_LOOPBACK flag enables loopback recording.
	In loopback recording, the audio engine copies the audio stream
	that is being played by a rendering endpoint device into an audio endpoint buffer
	so that a WASAPI client can capture the stream.
	If this flag is set, the IAudioClient::Initialize method attempts to open a capture buffer on the rendering device.
	This flag is valid only for a rendering device
	and only if the Initialize call sets the ShareMode parameter to AUDCLNT_SHAREMODE_SHARED.
	Otherwise the Initialize call will fail.
	If the call succeeds,
	the client can call the IAudioClient::GetService method
	to obtain an IAudioCaptureClient interface on the rendering device.
	For more information, see Loopback Recording.
	*/
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK, // ����ģʽ�£���Ƶengine�Ὣrending�豸���ڲ��ŵ���Ƶ���� ����һ�ݵ���Ƶ��endpoint buffer
									  // �����Ļ���WASAPI client���Բɼ���the stream.
									  // ���AUDCLNT_STREAMFLAGS_LOOPBACK�����ã�IAudioClient::Initialize�᳢��
									  // ��rending�豸����һ��capture buffer��
									  // AUDCLNT_STREAMFLAGS_LOOPBACKֻ��rending�豸��Ч��
									  // Initialize����AUDCLNT_SHAREMODE_SHAREDʱ�ſ���ʹ��, ����Initialize��ʧ�ܡ�
									  // Initialize�ɹ��󣬿�����IAudioClient::GetService�ɻ�ȡ��rending�豸��IAudioCaptureClient�ӿڡ�
		hnsRequestedDuration,
		0,
		pwfx,
		NULL);
#endif
	EXIT_ON_ERROR(hr)

		/*
		https://msdn.microsoft.com/en-us/library/windows/desktop/dd370874(v=vs.85).aspx
		The GetStreamLatency method retrieves the maximum latency for the current stream
		and can be called any time after the stream has been initialized.
		This method requires prior initialization of the IAudioClient interface.
		All calls to this method will fail with the error AUDCLNT_E_NOT_INITIALIZED until
		the client initializes the audio stream by successfully calling the IAudioClient::Initialize method.
		This method retrieves the maximum latency for the current stream.
		The value will not change for the lifetime of the IAudioClient object.
		Rendering clients can use this latency value to compute the minimum amount of data
		that they can write during any single processing pass.
		To write less than this minimum is to risk introducing glitches into the audio stream.
		For more information, see IAudioRenderClient::GetBuffer.

		1. �ú������ص�ǰ���������ʱ ��IAudioClient��������������� ���ᷢ���仯
		2. Rendering�ͻ��˿����������ʱֵ��������ÿ�δ���pass����д����С��������
		ע: ʹ��ǰ���ȵ���IAudioClient::Initialize
		*/

	REFERENCE_TIME hnsStreamLatency;
	hr = pAudioClient->GetStreamLatency(&hnsStreamLatency);
	EXIT_ON_ERROR(hr)

	cout << "GetStreamLatency     : " << hnsStreamLatency
		<< " REFERENCE_TIME time units. ��(" << hnsStreamLatency / 10000 << "ms)" << endl;

	/*
	phnsDefaultDevicePeriod [out]
	Pointer to a REFERENCE_TIME variable into which the method writes a time value
	specifying the default interval between periodic processing passes by the audio engine.
	The time is expressed in 100-nanosecond units.

	phnsMinimumDevicePeriod [out]
	Pointer to a REFERENCE_TIME variable into which the method writes a time value
	specifying the minimum interval between periodic processing passes by the audio endpoint device.
	The time is expressed in 100-nanosecond units.
	*/

	REFERENCE_TIME hnsDefaultDevicePeriod;
	REFERENCE_TIME hnsMinimumDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, &hnsMinimumDevicePeriod);
	EXIT_ON_ERROR(hr)

		cout << "GetDevicePeriod  ...\n"
		<< "hnsDefaultDevicePeriod : " << hnsDefaultDevicePeriod
		<< " REFERENCE_TIME time units. ��(" << hnsDefaultDevicePeriod / 10000 << "ms)" << endl
		<< "hnsMinimumDevicePeriod : " << hnsMinimumDevicePeriod
		<< " REFERENCE_TIME time units. ��(" << hnsMinimumDevicePeriod / 10000 << "ms)" << endl;

	// Get the size of the allocated buffer.
	// ���buffersize��ָ���ǻ����������Դ�Ŷ���֡��������
	/*
	https://msdn.microsoft.com/en-us/library/dd370866(v=vs.85).aspx
	HRESULT GetBufferSize(
	[out] UINT32 *pNumBufferFrames
	);

	pNumBufferFrames [out]
	Pointer to a UINT32 variable into which the method writes the number of audio frames
	that the buffer can hold.
	The IAudioClient::Initialize method allocates the buffer.
	The client specifies the buffer length in the hnsBufferDuration parameter value
	that it passes to the Initialize method. For rendering clients,
	the buffer length determines the maximum amount of rendering data
	that the application can write to the endpoint buffer during a single processing pass.
	For capture clients, the buffer length determines the maximum amount of capture data
	that the audio engine can read from the endpoint buffer during a single processing pass.

	The client should always call GetBufferSize after calling Initialize
	to determine the actual size of the allocated buffer,
	which might differ from the requested size.
	Rendering clients can use this value to calculate the largest rendering buffer size
	that can be requested from IAudioRenderClient::GetBuffer during each processing pass.
	*/
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	EXIT_ON_ERROR(hr)
		cout << endl << "GetBufferSize        : " << bufferFrameCount << endl;

	// SetEventHandle
	//////////////////////////////////////////////////////////////////////////
	HANDLE hAudioSamplesReadyEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
	if (hAudioSamplesReadyEvent == NULL)
	{
		printf("Unable to create samples ready event: %d.\n", GetLastError());
		goto Exit;
	}

	/*
	The SetEventHandle method sets the event handle that the system signals
	when an audio buffer is ready to be processed by the client.
	����Ƶ��buffer���� �ɱ�client����ʱ�� �ᷢ��ϵͳ�ź�
	SetEventHandle�������ô�����źŵ�event��handle
	During stream initialization, the client can, as an option, enable event-driven buffering.
	To do so, the client calls the IAudioClient::Initialize method
	with the AUDCLNT_STREAMFLAGS_EVENTCALLBACK flag set.
	After enabling event-driven buffering,
	and before calling the IAudioClient::Start method to start the stream,
	the client must call SetEventHandle to register the event handle
	that the system will signal each time a buffer becomes ready to be processed by the client.
	ʹ��SetEventHandle�� ��Ҫ��IAudioClient::Initialize����AUDCLNT_STREAMFLAGS_EVENTCALLBACK��
	SetEventHandleӦ���ڵ���IAudioClient::Start֮ǰ���á�
	*/
	hr = pAudioClient->SetEventHandle(hAudioSamplesReadyEvent);
	if (FAILED(hr))
	{
		printf("Unable to set ready event: %x.\n", hr);
		return false;
	}
	//////////////////////////////////////////////////////////////////////////

	// �����ɼ�����ӿ�
	hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->Start();  // Start recording.
	EXIT_ON_ERROR(hr)

	printf("\nAudio Capture begin...\n\n");

	int  nCnt = 0;

	size_t nCaptureBufferSize = 8 * 1024 * 1024;
	size_t nCurrentCaptureIndex = 0;

	BYTE *pbyCaptureBuffer = new (std::nothrow) BYTE[nCaptureBufferSize];

	HANDLE waitArray[3];
	waitArray[0] = hAudioSamplesReadyEvent;

	bool stillPlaying = true;

	// Each loop fills about half of the shared buffer.
	while (stillPlaying)
	{
		DWORD waitResult = WaitForMultipleObjects(1, waitArray, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0 + 0:     // _AudioSamplesReadyEvent
			hr = pCaptureClient->GetNextPacketSize(&packetLength);
			EXIT_ON_ERROR(hr)

			printf("%06d # _AudioSamplesReadyEvent packetLength:%06u \n", nCnt, packetLength);

			while (packetLength != 0)
			{
				// Get the available data in the shared buffer.
				// ��������������ȡ����
				hr = pCaptureClient->GetBuffer(&pData,
					&numFramesAvailable,
					&flags, NULL, NULL);
				EXIT_ON_ERROR(hr)


				nCnt++;

				// test flags
				//////////////////////////////////////////////////////////////////////////
				if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					printf("AUDCLNT_BUFFERFLAGS_SILENT \n");
				}

				if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
				{
					printf("%06d # AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY \n", nCnt);
				}
				//////////////////////////////////////////////////////////////////////////

				UINT32 framesToCopy = min(numFramesAvailable, static_cast<UINT32>((nCaptureBufferSize - nCurrentCaptureIndex) / nFrameSize));
				if (framesToCopy != 0)
				{
					//
					//  The flags on capture tell us information about the data.
					//
					//  We only really care about the silent flag since we want to put frames of silence into the buffer
					//  when we receive silence.  We rely on the fact that a logical bit 0 is silence for both float and int formats.
					//
					if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
					{
						//
						//  Fill 0s from the capture buffer to the output buffer.
						//
						ZeroMemory(&pbyCaptureBuffer[nCurrentCaptureIndex], framesToCopy*nFrameSize);
					}
					else
					{
						//
						//  Copy data from the audio engine buffer to the output buffer.
						//
						CopyMemory(&pbyCaptureBuffer[nCurrentCaptureIndex], pData, framesToCopy*nFrameSize);
					}
					//
					//  Bump the capture buffer pointer.
					//
					nCurrentCaptureIndex += framesToCopy*nFrameSize;
				}

				hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
				EXIT_ON_ERROR(hr)

					hr = pCaptureClient->GetNextPacketSize(&packetLength);
				EXIT_ON_ERROR(hr)

					// test GetCurrentPadding
					//////////////////////////////////////////////////////////////////////////
					/*
					This method retrieves a padding value that indicates the amount of
					valid, unread data that the endpoint buffer currently contains.
					����buffer�кϷ���δ��ȡ�����ݴ�С��
					The padding value is expressed as a number of audio frames.
					The size in bytes of an audio frame equals
					the number of channels in the stream multiplied by the sample size per channel.
					For example, the frame size is four bytes for a stereo (2-channel) stream with 16-bit samples.
					The padding value�ĵ�λ��audio frame��
					һ��audio frame�Ĵ�С���� ͨ���� * ÿ��ͨ����sample��С��
					For a shared-mode capture stream, the padding value reported by GetCurrentPadding
					specifies the number of frames of capture data
					that are available in the next packet in the endpoint buffer.
					*/
					UINT32 ui32NumPaddingFrames;
				hr = pAudioClient->GetCurrentPadding(&ui32NumPaddingFrames);
				EXIT_ON_ERROR(hr)
					if (0 != ui32NumPaddingFrames)
					{
						printf("GetCurrentPadding : %6u\n", ui32NumPaddingFrames);
					}
				//////////////////////////////////////////////////////////////////////////

				// �ɼ�һ����Ŀ��buffer���˳�
				if (nCnt == 1000)
				{
					stillPlaying = false;
					break;
				}

			} // end of 'while (packetLength != 0)'

			break;
		} // end of 'switch (waitResult)'

	} // end of 'while (stillPlaying)'

	  //
	  //  We've now captured our wave data.  Now write it out in a wave file.
	  //
	SaveWaveData(pbyCaptureBuffer, nCurrentCaptureIndex, pwfx);

	printf("\nAudio Capture Done.\n");

	hr = pAudioClient->Stop();  // Stop recording.
	EXIT_ON_ERROR(hr)

		Exit:
	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator)
		SAFE_RELEASE(pDevice)
		SAFE_RELEASE(pAudioClient)
		SAFE_RELEASE(pCaptureClient)

		CoUninitialize();

	//if (pbyCaptureBuffer)
	//{
	//	delete[] pbyCaptureBuffer;
	//	pbyCaptureBuffer = NULL;
	//}

	//if (hAudioSamplesReadyEvent)
	//{
	//	CloseHandle(hAudioSamplesReadyEvent);
	//	hAudioSamplesReadyEvent = NULL;
	//}

	getchar();

	return 0;
}