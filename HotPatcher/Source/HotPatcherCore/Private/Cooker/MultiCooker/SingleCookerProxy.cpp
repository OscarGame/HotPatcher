#include "Cooker/MultiCooker/SingleCookerProxy.h"
#include "FlibHotPatcherCoreHelper.h"
#include "HotPatcherCore.h"
#include "Cooker/MultiCooker/FlibHotCookerHelper.h"
#include "ShaderCompiler.h"
#include "Async/ParallelFor.h"
#include "ShaderPatch/FlibShaderCodeLibraryHelper.h"
#include "ThreadUtils/FThreadUtils.hpp"
#include "Cooker/MultiCooker/FCookShaderCollectionProxy.h"
#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"
#include "Misc/ScopeExit.h"
#include "Engine/AssetManager.h"
#if WITH_PACKAGE_CONTEXT
// // engine header
#include "UObject/SavePackage.h"
#endif

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 25
#include "Serialization/BulkDataManifest.h"
#endif


void USingleCookerProxy::Init(FPatcherEntitySettingBase* InSetting)
{
	Super::Init(InSetting);
	// IConsoleVariable* StreamableDelegateDelayFrames = IConsoleManager::Get().FindConsoleVariable(TEXT("s.StreamableDelegateDelayFrames"));
	// StreamableDelegateDelayFrames->Set(0);
	
#if WITH_PACKAGE_CONTEXT
	if(GetSettingObject()->bOverrideSavePackageContext)
	{
		PlatformSavePackageContexts = GetSettingObject()->PlatformSavePackageContexts;
	}
	else
	{
		PlatformSavePackageContexts = UFlibHotPatcherCoreHelper::CreatePlatformsPackageContexts(GetSettingObject()->CookTargetPlatforms,GetSettingObject()->IoStoreSettings.bIoStore);
	}
#endif
	InitShaderLibConllections();
	// cook package tracker
	if(GetSettingObject()->bPackageTracker)
	{
		PackagePathSet.PackagePaths.Append(GetSettingObject()->SkipLoadedAssets);
		for(const auto& Asset:GetSettingObject()->CookAssets)
		{
			PackagePathSet.PackagePaths.Add(*UFlibAssetManageHelper::PackagePathToLongPackageName(Asset.PackagePath.ToString()));
		}
		PackageTracker = MakeShareable(new FPackageTracker(PackagePathSet.PackagePaths));
	}
	UFlibHotPatcherCoreHelper::DeleteDirectory(GetSettingObject()->StorageMetadataDir);
}

void USingleCookerProxy::Tick(float DeltaTime)
{
	
}

TStatId USingleCookerProxy::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USingleCookerProxy, STATGROUP_Tickables);
}

bool USingleCookerProxy::IsTickable() const
{
	return true;
}

void USingleCookerProxy::Shutdown()
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::Shutdown",FColor::Red);
	WaitCookerFinished();
	ShutdowShaderLibCollections();
	
	if(PackageTracker)
	{
		auto SerializePackageSetToString = [](const TSet<FName>& Packages)->FString
		{
			FString OutString;
			FPackagePathSet AdditionalPackageSet;
			AdditionalPackageSet.PackagePaths.Append(Packages);
			if(AdditionalPackageSet.PackagePaths.Num())
			{
				THotPatcherTemplateHelper::TSerializeStructAsJsonString(AdditionalPackageSet,OutString);
			}
			return OutString;
		};
		
		FFileHelper::SaveStringToFile(SerializePackageSetToString(PackageTracker->GetPendingPackageSet()),*FPaths::Combine(GetSettingObject()->StorageMetadataDir,TEXT("AdditionalPackageSet.json")));
		FFileHelper::SaveStringToFile(SerializePackageSetToString(PackageTracker->GetLoadedPackages()),*FPaths::Combine(GetSettingObject()->StorageMetadataDir,TEXT("AllLoadedPackageSet.json")));
	}
	BulkDataManifest();
	IoStoreManifest();
	Super::Shutdown();
}
//
// void USingleCookerProxy::DoCookMission(const TArray<FAssetDetail>& Assets)
// {
//
// }

void USingleCookerProxy::BulkDataManifest()
{
	// save bulk data manifest
	for(auto& Platform:GetSettingObject()->CookTargetPlatforms)
	{
		if(GetSettingObject()->IoStoreSettings.bStorageBulkDataInfo)
		{
#if WITH_PACKAGE_CONTEXT
			UFlibHotPatcherCoreHelper::SavePlatformBulkDataManifest(GetPlatformSavePackageContexts(),Platform);
#endif
		}
	}
}

void USingleCookerProxy::IoStoreManifest()
{
	if(GetSettingObject()->IoStoreSettings.bIoStore)
	{
		TSet<ETargetPlatform> Platforms;
		for(const auto& Cluser:CookClusters)
		{
			for(auto Platform:Cluser.Platforms)
			{
				Platforms.Add(Platform);
			}
		}

		for(auto Platform:Platforms)
		{
			FString PlatformName = THotPatcherTemplateHelper::GetEnumNameByValue(Platform);
			TimeRecorder StorageCookOpenOrderTR(FString::Printf(TEXT("Storage CookOpenOrder.txt for %s, time:"),*PlatformName));
			struct CookOpenOrder
			{
				CookOpenOrder()=default;
				CookOpenOrder(const FString& InPath,int32 InOrder):uasset_relative_path(InPath),order(InOrder){}
				FString uasset_relative_path;
				int32 order;
			};
			auto MakeCookOpenOrder = [](const TArray<FName>& Assets)->TArray<CookOpenOrder>
			{
				TArray<CookOpenOrder> result;
				TArray<FAssetData> AssetsData;
				TArray<FString> AssetPackagePaths;
				for (auto AssetPackagePath : Assets)
				{
					FSoftObjectPath ObjectPath{ AssetPackagePath.ToString() };
					TArray<FAssetData> AssetData;
					UFlibAssetManageHelper::GetSpecifyAssetData(ObjectPath.GetLongPackageName(),AssetData,true);
					AssetsData.Append(AssetData);
				}

				// UFlibAssetManageHelper::GetAssetsData(AssetPackagePaths,AssetsData);
					
				for(int32 index=0;index<AssetsData.Num();++index)
				{
					UPackage* Package = AssetsData[index].GetPackage();
					FString LocalPath;
					const FString* PackageExtension = Package->ContainsMap() ? &FPackageName::GetMapPackageExtension() : &FPackageName::GetAssetPackageExtension();
					FPackageName::TryConvertLongPackageNameToFilename(AssetsData[index].PackageName.ToString(), LocalPath, *PackageExtension);
					result.Emplace(LocalPath,index);
				}
				return result;
			};
			TArray<CookOpenOrder> CookOpenOrders = MakeCookOpenOrder(GetPlatformCookAssetOrders(Platform));

			auto SaveCookOpenOrder = [](const TArray<CookOpenOrder>& CookOpenOrders,const FString& File)
			{
				TArray<FString> result;
				for(const auto& OrderFile:CookOpenOrders)
				{
					result.Emplace(FString::Printf(TEXT("\"%s\" %d"),*OrderFile.uasset_relative_path,OrderFile.order));
				}
				FFileHelper::SaveStringArrayToFile(result,*FPaths::ConvertRelativePathToFull(File));
			};
			SaveCookOpenOrder(CookOpenOrders,FPaths::Combine(GetSettingObject()->StorageMetadataDir,GetSettingObject()->MissionName,PlatformName,TEXT("CookOpenOrder.txt")));
		}
	}
}

void USingleCookerProxy::InitShaderLibConllections()
{
	FString SavePath = GetSettingObject()->StorageMetadataDir;
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::InitShaderLibConllections",FColor::Red);
	PlatformCookShaderCollection = UFlibHotCookerHelper::CreateCookShaderCollectionProxyByPlatform(
		GetSettingObject()->ShaderLibName,
		GetSettingObject()->CookTargetPlatforms,
		GetSettingObject()->ShaderOptions.bSharedShaderLibrary,
		GetSettingObject()->ShaderOptions.bNativeShader,
		true,
		SavePath,
		true
	);
	
	if(PlatformCookShaderCollection.IsValid())
	{
		PlatformCookShaderCollection->Init();
	}
}

void USingleCookerProxy::ShutdowShaderLibCollections()
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::ShutdowShaderLibCollections",FColor::Red);
	if(GetSettingObject()->ShaderOptions.bSharedShaderLibrary)
	{
		if(PlatformCookShaderCollection.IsValid())
		{
			PlatformCookShaderCollection->Shutdown();
		}
	}
}

FCookCluster USingleCookerProxy::GetPackageTrackerAsCluster()
{
	FCookCluster PackageTrackerCluster;
		
	PackageTrackerCluster.Platforms = GetSettingObject()->CookTargetPlatforms;
	PackageTrackerCluster.bPreGeneratePlatformData = GetSettingObject()->bPreGeneratePlatformData;

	PackageTrackerCluster.CookActionCallback.OnAssetCooked = GetOnPackageSavedCallback();
	PackageTrackerCluster.CookActionCallback.OnCookBegin = GetOnCookAssetBeginCallback();
			
	if(PackageTracker && GetSettingObject()->bCookAdditionalAssets)
	{
		PackageTrackerCluster.AssetDetails.Empty();
		for(FName PackagePath:PackageTracker->GetPendingPackageSet())
		{
					
			PackageTrackerCluster.AssetDetails.Emplace(UFlibAssetManageHelper::GetAssetDetailByPackageName(PackagePath.ToString()));
		}
	}
	return PackageTrackerCluster;
};
FCookActionResultEvent USingleCookerProxy::GetOnPackageSavedCallback()
{
	const FCookActionResultEvent PackageSavedCallback = [this](const FSoftObjectPath& PackagePath,ETargetPlatform Platform,ESavePackageResult Result)
	{
		OnAssetCooked.Broadcast(PackagePath,Platform,Result);
	};
	return PackageSavedCallback;
}

FCookActionEvent USingleCookerProxy::GetOnCookAssetBeginCallback()
{
	const FCookActionEvent CookAssetBegin = [this](const FSoftObjectPath& PackagePath,ETargetPlatform Platform)
	{
		OnCookAssetBegin.Broadcast(PackagePath,Platform);
	};
	return CookAssetBegin;
}

bool USingleCookerProxy::DoExport()
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::DoExport",FColor::Red);
	GetCookFailedAssetsCollection().MissionName = GetSettingObject()->MissionName;
	GetCookFailedAssetsCollection().MissionID = GetSettingObject()->MissionID;
	GetCookFailedAssetsCollection().CookFailedAssets.Empty();

	OnAssetCooked.AddUObject(this,&USingleCookerProxy::OnAssetCookedHandle);
	
	{
		SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::DoCookMission",FColor::Red);
		
		FCookCluster DefaultCluser;
		DefaultCluser.AssetDetails = GetSettingObject()->CookAssets;
		
		DefaultCluser.Platforms = GetSettingObject()->CookTargetPlatforms;
		DefaultCluser.bPreGeneratePlatformData = GetSettingObject()->bPreGeneratePlatformData;

		DefaultCluser.CookActionCallback.OnAssetCooked = GetOnPackageSavedCallback();
		DefaultCluser.CookActionCallback.OnCookBegin = GetOnCookAssetBeginCallback();
		
		if(DefaultCluser.bPreGeneratePlatformData)
		{
			PreGeneratePlatformData(DefaultCluser);
			if(PackageTracker && GetSettingObject()->bCookAdditionalAssets)
			{
				PreGeneratePlatformData(GetPackageTrackerAsCluster());
			}
		}
		
		CookCluster(DefaultCluser);
		if(PackageTracker && GetSettingObject()->bCookAdditionalAssets)
		{
			// cook all additional assets
			CookCluster(GetPackageTrackerAsCluster());
		}
	}
	if(HasError())
	{
		FString FailedJsonString;
		THotPatcherTemplateHelper::TSerializeStructAsJsonString(GetCookFailedAssetsCollection(),FailedJsonString);
		UE_LOG(LogHotPatcher,Warning,TEXT("Single Cooker Proxy %s:\n%s"),*GetSettingObject()->MissionName,*FailedJsonString);
		FString SaveTo = UFlibHotCookerHelper::GetCookerProcFailedResultPath(GetSettingObject()->StorageMetadataDir,GetSettingObject()->MissionName,GetSettingObject()->MissionID);
		FFileHelper::SaveStringToFile(FailedJsonString,*SaveTo);
	}
	
	return !HasError();
}
FName WorldType = TEXT("World");


void USingleCookerProxy::CookClusterSync(const FCookCluster& CookCluster)
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::CookClusterSync",FColor::Red);
	
	TArray<ITargetPlatform*> TargetPlatforms = UFlibHotPatcherCoreHelper::GetTargetPlatformsByNames(CookCluster.Platforms);

	if(GetSettingObject()->bConcurrentSave)
	{
		FString CookBaseDir = GetSettingObject()->StorageCookedDir;
#if WITH_PACKAGE_CONTEXT
		TMap<FString, FSavePackageContext*> SavePackageContextsNameMapping = GetPlatformSavePackageContextsNameMapping();
#endif
		TArray<UPackage*> AllPackages = UFlibAssetManageHelper::LoadPackagesForCooking(CookCluster.AsSoftObjectPaths(),GetSettingObject()->bConcurrentSave);
		
		GIsSavingPackage = true;
		TMap<FName,TMap<FName,FString>> PackageCookedSavePaths;
		for(const auto& Package:AllPackages)
		{
			FName PackagePathName = *Package->GetPathName();
			PackageCookedSavePaths.Add(PackagePathName,TMap<FName,FString>{});
			for(auto Platform:TargetPlatforms)
			{
				FString CookedSavePath = UFlibHotPatcherCoreHelper::GetAssetCookedSavePath(CookBaseDir,PackagePathName.ToString(), Platform->PlatformName());
				PackageCookedSavePaths.Find(PackagePathName)->Add(*Platform->PlatformName(),CookedSavePath);
			}
		}
		
		ParallelFor(AllPackages.Num(), [=](int32 AssetIndex)
		{
			UFlibHotPatcherCoreHelper::CookPackage(
				AllPackages[AssetIndex],
				TargetPlatforms,
				CookCluster.CookActionCallback,
#if WITH_PACKAGE_CONTEXT
				SavePackageContextsNameMapping,
#endif
				*PackageCookedSavePaths.Find(*AllPackages[AssetIndex]->GetPathName()),
				true);
		},!GetSettingObject()->bConcurrentSave);
		GIsSavingPackage = false;
	}
	else
	{
		UFlibHotPatcherCoreHelper::CookAssets(CookCluster.AsSoftObjectPaths(),CookCluster.Platforms,CookCluster.CookActionCallback
#if WITH_PACKAGE_CONTEXT
									,GetPlatformSavePackageContextsRaw()
#endif
									,GetSettingObject()->StorageCookedDir
);
	}
	
}

void USingleCookerProxy::PreGeneratePlatformData(const FCookCluster& CookCluster)
{
	TArray<ITargetPlatform*> TargetPlatforms = UFlibHotPatcherCoreHelper::GetTargetPlatformsByNames(CookCluster.Platforms);
	bool bConcurrentSave = GetSettingObject()->bConcurrentSave;
	TSet<UObject*> ProcessedObjects;
	TSet<UObject*> PendingCachePlatformDataObjects;
	if(CookCluster.bPreGeneratePlatformData)
	{
		FCookCluster Cluster = CookCluster;

		auto PreCachePlatformDataForPackages = [&](TArray<UPackage*>& PreCachePackages,const FString& Display)
		{
			uint32 TotalPackagesNum = PreCachePackages.Num();
			for(int32 Index = PreCachePackages.Num() - 1 ;Index >= 0;--Index)
			{
				UPackage* CurrentPackage = PreCachePackages[Index];
				if(GCookLog)
				{
					UE_LOG(LogHotPatcher,Display,TEXT("PreCache %s, pending %d total %d"),*CurrentPackage->GetPathName(),PreCachePackages.Num(),TotalPackagesNum);
				}
				
				UFlibHotPatcherCoreHelper::CacheForCookedPlatformData(
					TArray<UPackage*>{CurrentPackage},
					TargetPlatforms,
					ProcessedObjects,
					PendingCachePlatformDataObjects,
					bConcurrentSave,
					GetSettingObject()->bWaitEachAssetCompleted);
				PreCachePackages.RemoveAtSwap(Index,1,false);
			}
			UFlibHotPatcherCoreHelper::WaitObjectsCachePlatformDataComplete(ProcessedObjects,PendingCachePlatformDataObjects,TargetPlatforms);
			// GEngine->ForceGarbageCollection();
		};
		
		for(auto Class:GetPreCacheClasses())
		{
			FString DisplayStr = FString::Printf(TEXT("PreCache DDC For %s"),*Class->GetName());
			FScopedNamedEvent CacheClassEvent(FColor::Red,*DisplayStr);
			UE_LOG(LogHotPatcher,Log,TEXT("%s"),*DisplayStr);
			TArray<FAssetDetail> ClassAssetDetails = THotPatcherTemplateHelper::GetArrayBySrcWithCondition<FAssetDetail>(Cluster.AssetDetails,[&](FAssetDetail AssetDetail)->bool
				{
					return AssetDetail.AssetType.IsEqual(*Class->GetName());
				},true);
			TArray<FSoftObjectPath> ObjectPaths;
			for(const auto& AssetDetail:ClassAssetDetails)
			{
				ObjectPaths.Emplace(AssetDetail.PackagePath.ToString());
			}
			
			TArray<UPackage*> PreCachePackages = UFlibAssetManageHelper::LoadPackagesForCooking(ObjectPaths,GetSettingObject()->bConcurrentSave);
			PreCachePlatformDataForPackages(PreCachePackages,Class->GetName());
		}
		TArray<UPackage*> OthesPackages = UFlibAssetManageHelper::LoadPackagesForCooking(Cluster.AsSoftObjectPaths(),GetSettingObject()->bConcurrentSave);
		PreCachePlatformDataForPackages(OthesPackages,TEXT("Global"));
	}
}

void USingleCookerProxy::CleanOldCooked(const FString& CookBaseDir,const TArray<FSoftObjectPath>& ObjectPaths,const TArray<ETargetPlatform>& CookPlatforms)
{
	TArray<ITargetPlatform*> CookPlatfotms = UFlibHotPatcherCoreHelper::GetTargetPlatformsByNames(CookPlatforms);
	{
		SCOPED_NAMED_EVENT_TEXT("Delete Old Cooked Files",FColor::Red);
		// FString CookBaseDir = GetSettingObject()->StorageCookedDir;
		TArray<FString> PaddingDeleteFiles;
		for(const auto& Asset:ObjectPaths)
		{
			FString PackageName = Asset.GetLongPackageName();
			for(const auto& TargetPlatform:CookPlatfotms)
			{
				FString CookedSavePath = UFlibHotPatcherCoreHelper::GetAssetCookedSavePath(CookBaseDir,PackageName, TargetPlatform->PlatformName());
				PaddingDeleteFiles.AddUnique(CookedSavePath);
			}
		}
	
		ParallelFor(PaddingDeleteFiles.Num(),[PaddingDeleteFiles](int32 Index)
		{
			FString FileName = PaddingDeleteFiles[Index];
			if(!FileName.IsEmpty() && FPaths::FileExists(FileName))
			{
				IFileManager::Get().Delete(*FileName,true,true,true);
			}
		},false);
	}
}

void USingleCookerProxy::CookCluster(const FCookCluster& CookCluster)
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::CookCluster",FColor::Red);

	CleanOldCooked(GetSettingObject()->StorageCookedDir,CookCluster.AsSoftObjectPaths(),CookCluster.Platforms);
	
	CookClusterSync(CookCluster);
}

void USingleCookerProxy::AddCluster(const FCookCluster& CookCluster)
{
#if WITH_PACKAGE_CONTEXT
	for(auto Platform:CookCluster.Platforms)
	{
		if(!GetPlatformSavePackageContexts().Contains(Platform))
		{
			GetPlatformSavePackageContexts().Append(UFlibHotPatcherCoreHelper::CreatePlatformsPackageContexts(TArray<ETargetPlatform>{Platform},GetSettingObject()->IoStoreSettings.bIoStore));
		}
	}
#endif
	
	for(const auto& AssetDetail:CookCluster.AssetDetails)
	{
		FSoftObjectPath SoftObjectPath(AssetDetail.PackagePath.ToString());
		FName AssetType = UFlibAssetManageHelper::GetAssetType(SoftObjectPath);
		if(!AssetType.IsNone())
		{
			GetAssetTypeMapping().Add(SoftObjectPath.GetAssetPathName(),AssetType);
		}
	}
	CookClusters.Add(CookCluster);
}


bool USingleCookerProxy::HasError()
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::HasError",FColor::Red);
	TArray<ETargetPlatform> TargetPlatforms;
	GetCookFailedAssetsCollection().CookFailedAssets.GetKeys(TargetPlatforms);
	return !!TargetPlatforms.Num();
}

void USingleCookerProxy::OnAssetCookedHandle(const FSoftObjectPath& PackagePath, ETargetPlatform Platform,
	ESavePackageResult Result)
{
	FScopeLock Lock(&SynchronizationObject);
	if(Result == ESavePackageResult::Success)
	{
		GetPaendingCookAssetsSet().Remove(PackagePath.GetAssetPathName());
		MarkAssetCooked(PackagePath,Platform);
	}
	else
	{
		SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::OnCookAssetFailed",FColor::Red);
		FString PlatformName = THotPatcherTemplateHelper::GetEnumNameByValue(Platform);
		UE_LOG(LogHotPatcher,Warning,TEXT("Cook %s for %s Failed!"),*PackagePath.GetAssetPathString(),*PlatformName);
		GetPaendingCookAssetsSet().Remove(PackagePath.GetAssetPathName());
	}
}

bool USingleCookerProxy::IsFinsihed()
{
	return !GetPaendingCookAssetsSet().Num();
	// &&
	// 	!GetPenddingCacheObjects().Num() &&
	// 		!GShaderCompilingManager->IsCompiling();
}

void USingleCookerProxy::WaitCookerFinished()
{
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::WaitCookerFinished",FColor::Red);
	// Wait for all shaders to finish compiling
	// UFlibHotPatcherCoreHelper::WaitObjectsCachePlatformDataComplete(ProcessedObjects,PendingCachePlatformDataObjects,TargetPlatforms);
	UFlibShaderCodeLibraryHelper::WaitShaderCompilingComplete();
	UFlibHotPatcherCoreHelper::WaitForAsyncFileWrites();
}
#if WITH_PACKAGE_CONTEXT
TMap<ETargetPlatform, FSavePackageContext*> USingleCookerProxy::GetPlatformSavePackageContextsRaw()
{
	FScopeLock Lock(&SynchronizationObject);
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::GetPlatformSavePackageContextsRaw",FColor::Red);
	TMap<ETargetPlatform,FSavePackageContext*> result;
	TArray<ETargetPlatform> Keys;
	GetPlatformSavePackageContexts().GetKeys(Keys);
	for(const auto& Key:Keys)
	{
		result.Add(Key,GetPlatformSavePackageContexts().Find(Key)->Get());
	}
	return result;
}


TMap<FString, FSavePackageContext*> USingleCookerProxy::GetPlatformSavePackageContextsNameMapping()
{
	FScopeLock Lock(&SynchronizationObject);
	SCOPED_NAMED_EVENT_TEXT("USingleCookerProxy::GetPlatformSavePackageContextsNameMapping",FColor::Red);
	TMap<FString,FSavePackageContext*> result;
	TArray<ETargetPlatform> Keys;
	GetPlatformSavePackageContexts().GetKeys(Keys);
	for(const auto& Key:Keys)
	{
		result.Add(THotPatcherTemplateHelper::GetEnumNameByValue(Key),GetPlatformSavePackageContexts().Find(Key)->Get());
	}
	return result;
}
#endif

TArray<FName>& USingleCookerProxy::GetPlatformCookAssetOrders(ETargetPlatform Platform)
{
	FScopeLock Lock(&SynchronizationObject);
	return CookAssetOrders.FindOrAdd(Platform);
}

TSet<FName> USingleCookerProxy::GetAdditionalAssets()
{
	if(GetSettingObject()->bPackageTracker && PackageTracker.IsValid())
	{
		return PackageTracker->GetPendingPackageSet();
	}
	return TSet<FName>{};
}

TArray<UClass*> USingleCookerProxy::GetPreCacheClasses() const
{
	TArray<UClass*> Classes;

	TSet<UClass*> ParentClasses = {
		UTexture::StaticClass(),
		UMaterialInterface::StaticClass(),
		UMaterialFunctionInterface::StaticClass(),
		UMaterialExpression::StaticClass()
	};

	for(auto& ParentClass:ParentClasses)
	{
		Classes.Append(UFlibHotPatcherCoreHelper::GetDerivedClasses(ParentClass,true,true));
	}
	
	return Classes;
}

void USingleCookerProxy::MarkAssetCooked(const FSoftObjectPath& PackagePath, ETargetPlatform Platform)
{
	FScopeLock Lock(&SynchronizationObject);
	GetPlatformCookAssetOrders(Platform).Add(PackagePath.GetAssetPathName());
}


void USingleCookerProxy::AsyncLoadAssets(const TArray<FSoftObjectPath>& ObjectPaths)
{
	UAssetManager& AssetManager = UAssetManager::Get();
	FStreamableManager& StreamableManager = AssetManager.GetStreamableManager();
	StreamableManager.RequestAsyncLoad(ObjectPaths,FStreamableDelegate::CreateUObject(this,&USingleCookerProxy::OnAsyncAssetsLoaded));
}

void USingleCookerProxy::OnAsyncAssetsLoaded()
{
	
}
