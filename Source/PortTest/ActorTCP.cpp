// Fill out your copyright notice in the Description page of Project Settings.

#include "ActorTCP.h"
#include <string>


// Sets default values
AActorTCP::AActorTCP()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AActorTCP::BeginPlay()
{
	Super::BeginPlay();
	
}

void AActorTCP::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	UWorld* World = GetWorld();

	GetWorld()->GetTimerManager().ClearTimer(TCPConnectionListenerTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(TCPSocketListenerTimerHandle);

	if (ConnectionSocket != NULL) {
		ConnectionSocket->Close();
	}
	if (ListenerSocket != NULL) {
		ListenerSocket->Close();
	}

}

// Called every frame
void AActorTCP::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// TCP Server Code
bool AActorTCP::LaunchTCP()
{
	if (!StartTCPReceiver(SocketName, IpAddress, Port))
	{
		return false;
	}
	return true;
}

bool AActorTCP::StartTCPReceiver(const FString& YourChosenSocketName, const FString& TheIP, const int32 ThePort) {
	ListenerSocket = CreateTCPConnectionListener(YourChosenSocketName, TheIP, ThePort);

	//Not created?
	if (!ListenerSocket)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("StartTCPReceiver>> Listen socket could not be created! ~> %s %d"), *TheIP, ThePort));
		return false;
	}

	UWorld* World = GetWorld();
	World->GetTimerManager().SetTimer(TCPConnectionListenerTimerHandle, this, &AActorTCP::TCPConnectionListener, 1.0f, true);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("StartTCPReceiver>> Listen socket created")));
	return true;
}
//Format IP String as Number Parts
bool AActorTCP::FormatIP4ToNumber(const FString& TheIP, uint8(&Out)[4])
{
	TheIP.Replace(TEXT(" "), TEXT(""));

	TArray<FString> Parts;
	TheIP.ParseIntoArray(Parts, TEXT("."), true);
	if (Parts.Num() != 4)
		return false;

	for (int32 i = 0; i < 4; ++i)
	{
		Out[i] = FCString::Atoi(*Parts[i]);
	}

	return true;
}

FSocket* AActorTCP::CreateTCPConnectionListener(const FString& YourChosenSocketName, const FString& TheIP, const int32 ThePort, const int32 ReceiveBufferSize)
{
	uint8 IP4Nums[4];
	if (!FormatIP4ToNumber(TheIP, IP4Nums))
	{
		return false;
	}

	return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("default"), false);
}
//Rama's TCP Connection Listener
void AActorTCP::TCPConnectionListener()
{
	if (!ListenerSocket) return;

	TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	bool Pending;

	// handle incoming connections
	ListenerSocket->HasPendingConnection(Pending);

	if (!ConnectionSocket)
	{
		// kostyle
		uint8 IP4Nums[4];
		FormatIP4ToNumber(IpAddress, IP4Nums);
		FIPv4Address ip(IP4Nums[0], IP4Nums[1], IP4Nums[2], IP4Nums[3]);

		TSharedRef<FInternetAddr> addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		addr->SetIp(ip.Value);
		addr->SetPort(Port);

		ListenerSocket->Connect(*addr);
		ConnectionSocket = ListenerSocket;

		//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		//Already have a Connection? destroy previous
		//if (ConnectionSocket)
		//{
		//	ConnectionSocket->Close();
		//	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket);
		//}
		////~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

		////New Connection receive!
		//ConnectionSocket = ListenerSocket->Accept(*RemoteAddress, TEXT("RamaTCP Received Socket Connection"));


		if (ConnectionSocket != NULL)
		{
			//Global cache of current Remote Address
			RemoteAddressForConnection = FIPv4Endpoint(RemoteAddress);

			//can thread this too
			UWorld* World = GetWorld();

			World->GetTimerManager().SetTimer(TCPSocketListenerTimerHandle, this, &AActorTCP::TCPSocketListener, 0.1f, true);
		}
	}
}

FString AActorTCP::StringFromBinaryArray(TArray<uint8> BinaryArray)
{
	return FString::SanitizeFloat(*reinterpret_cast<double*>(BinaryArray.GetData()));
	//return BytesToHex(BinaryArray.GetData(), BinaryArray.Num());

	//BinaryArray.Add(0); // Add 0 termination. Even if the string is already 0-terminated, it doesn't change the results.
						// Create a string from a byte array. The string is expected to be 0 terminated (i.e. a byte set to 0).
						// Use UTF8_TO_TCHAR if needed.
						// If you happen to know the data is UTF-16 (USC2) formatted, you do not need any conversion to begin with.
						// Otherwise you might have to write your own conversion algorithm to convert between multilingual UTF-16 planes.
	//return FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(BinaryArray.GetData())));
}

void AActorTCP::TCPSend(FString ToSend) {
	ToSend = ToSend + LINE_TERMINATOR; //For Matlab we need a defined line break (fscanf function) "\n" ist not working, therefore use the LINE_TERMINATOR macro form UE

	TCHAR *SerializedChar = ToSend.GetCharArray().GetData();
	int32 Size = FCString::Strlen(SerializedChar);
	int32 Sent = 0;
	uint8* ResultChars = (uint8*)TCHAR_TO_UTF8(SerializedChar);

	if (!ConnectionSocket->Send(ResultChars, Size, Sent)) {
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Error sending message")));
	}

}

void AActorTCP::TCPSocketListener()
{
	//~~~~~~~~~~~~~
	if (!ConnectionSocket) return;
	//~~~~~~~~~~~~~


	//Binary Array!
	TArray<uint8> ReceivedData;

	uint32 Size;
	while (ConnectionSocket->HasPendingData(Size))
	{
		ReceivedData.Init(FMath::Min(Size, 65507u), Size);

		int32 Read = 0;
		ConnectionSocket->Recv(ReceivedData.GetData(), ReceivedData.Num(), Read);
	}
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	if (ReceivedData.Num() <= 0)
	{
		return;
	}

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	//						Rama's String From Binary Array
	const FString ReceivedUE4String = StringFromBinaryArray(ReceivedData);
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	recievedMessage(ReceivedUE4String);

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("As String Data ~> %s"), *ReceivedUE4String));
}

