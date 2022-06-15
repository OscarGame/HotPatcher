#pragma once
#include "FSingleCookerSettings.h"
#include "Cooker/HotPatcherCookerSettingBase.h"
#include "CreatePatch/HotPatcherProxyBase.h"
#include "HotPatcherBaseTypes.h"
#include "BaseTypes/FPackageTracker.h"
// engine header
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatform.h"
#include "Templates/SharedPointer.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ThreadUtils/FThreadUtils.hpp"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "SingleCookerProxy.generated.h"

USTRUCT()
struct FPackagePathSet
{
    GENERATED_BODY()

    UPROPERTY()
    TSet<FName> PackagePaths;
};

DECLARE_MULTICAST_DELEGATE(FSingleCookerEvent);
DECLARE_MULTICAST_DELEGATE_TwoParams(FSingleCookActionEvent,const FSoftObjectPath&,ETargetPlatform);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSingleCookResultEvent,const FSoftObjectPath&,ETargetPlatform,ESavePackageResult);

USTRUCT()
struct FCookCluster
{
    GENERATED_BODY()
    UPROPERTY()
    TArray<FAssetDetail> AssetDetails;
    UPROPERTY()
    TArray<ETargetPlatform> Platforms;
    UPROPERTY()
    bool bPreGeneratePlatformData = false;


    FORCEINLINE_DEBUGGABLE TArray<FSoftObjectPath> AsSoftObjectPaths()const
    {
        TArray<FSoftObjectPath> SoftObjectPaths;
        for(const auto& AssetDetail:AssetDetails)
        {
            if(AssetDetail.IsValid())
            {
                SoftObjectPaths.Emplace(AssetDetail.PackagePath.ToString());
            }
        }
        return SoftObjectPaths;
    }
    FCookActionCallback CookActionCallback;
};


UCLASS()
class HOTPATCHERCORE_API USingleCookerProxy:public UHotPatcherProxyBase, public FTickableEditorObject
{
    GENERATED_BODY()
public:
    virtual void Init(FPatcherEntitySettingBase* InSetting)override;

    
    virtual void Shutdown() override;
    virtual bool DoExport()override;
    bool IsFinsihed();
    bool HasError();
    virtual FSingleCookerSettings* GetSettingObject()override {return (FSingleCookerSettings*)(Setting);};

public:
    virtual void Tick(float DeltaTime) override;
    TStatId GetStatId() const override;
    bool IsTickable() const override;
public: // core interface
    FCookerFailedCollection& GetCookFailedAssetsCollection(){return CookFailedAssetsCollection;};
    void CleanOldCooked(const FString& CookBaseDir,const TArray<FSoftObjectPath>& ObjectPaths,const TArray<ETargetPlatform>& CookPlatforms);
    void CookClusterSync(const FCookCluster& CookCluster);
    void CookCluster(const FCookCluster& CookCluster);
    void AddCluster(const FCookCluster& CookCluster);

    void PreGeneratePlatformData(const FCookCluster& CookCluster);
    void WaitCookerFinished();
    FCookCluster GetPackageTrackerAsCluster();

    void AsyncLoadAssets(const TArray<FSoftObjectPath>& ObjectPaths);
    void OnAsyncAssetsLoaded();
    
public: // callback
    FCookActionResultEvent GetOnPackageSavedCallback();
    FCookActionEvent GetOnCookAssetBeginCallback();
    
public: // public get interface
    TArray<FName>& GetPlatformCookAssetOrders(ETargetPlatform Platform);
    TSet<FName> GetAdditionalAssets();
    TArray<UClass*> GetPreCacheClasses()const;
    FORCEINLINE_DEBUGGABLE TMap<FName,FName>& GetAssetTypeMapping(){ return AssetTypeMapping; }
    FORCEINLINE_DEBUGGABLE TSet<FName>& GetPaendingCookAssetsSet(){ return PaendingCookAssetsSet; }
    
protected:
    void OnAssetCookedHandle(const FSoftObjectPath& PackagePath,ETargetPlatform Platform,ESavePackageResult Result);
    void MarkAssetCooked(const FSoftObjectPath& PackagePath,ETargetPlatform Platform);

protected: // metadata
    void BulkDataManifest();
    void IoStoreManifest();
    
    void InitShaderLibConllections();
    void ShutdowShaderLibCollections();
    
private: // package context
#if WITH_PACKAGE_CONTEXT
    FORCEINLINE TMap<ETargetPlatform,TSharedPtr<FSavePackageContext>>& GetPlatformSavePackageContexts() {return PlatformSavePackageContexts;}
    TMap<ETargetPlatform,FSavePackageContext*> GetPlatformSavePackageContextsRaw();
    TMap<FString, FSavePackageContext*> GetPlatformSavePackageContextsNameMapping();
    TMap<ETargetPlatform,TSharedPtr<FSavePackageContext>> PlatformSavePackageContexts;
#endif

public: // delegate
    FSingleCookerEvent OnCookBegin;
    FSingleCookerEvent OnCookFinished;
    FSingleCookActionEvent OnCookAssetBegin;
    FSingleCookResultEvent OnAssetCooked;

protected:
    // FORCEINLINE TSet<UObject*>& GetPenddingCacheObjects() { return PendingCachePlatformDataObjects; };
    // UPROPERTY()
    // mutable TSet<UObject*> PendingCachePlatformDataObjects;
    // // UPROPERTY()
    // mutable TSet<UObject*> ProcessedObjects;

private:
    TMap<FName,FName> AssetTypeMapping;
    TSet<FName> PaendingCookAssetsSet;

    /** Critical section for synchronizing access to sink. */
    mutable FCriticalSection SynchronizationObject;
    
private:
    TArray<FCookCluster> CookClusters;
    FCookerFailedCollection CookFailedAssetsCollection;
    TSharedPtr<FPackageTracker> PackageTracker;
    TSharedPtr<FThreadWorker> WaitThreadWorker;
    TSharedPtr<struct FCookShaderCollectionProxy> PlatformCookShaderCollection;
    FPackagePathSet PackagePathSet;
    TMap<ETargetPlatform,TArray<FName>> CookAssetOrders;
};