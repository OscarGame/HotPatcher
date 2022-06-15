#include "CreatePatch/HotPatcherSettingBase.h"
#include "FlibPatchParserHelper.h"
#include "FPlatformExternFiles.h"
#include "HotPatcherLog.h"

FHotPatcherSettingBase::FHotPatcherSettingBase():bAnalysisFilterDependencies(true),
	AssetRegistryDependencyTypes(TArray<EAssetRegistryDependencyTypeEx>{EAssetRegistryDependencyTypeEx::Packages})
{
	ForceSkipContentRules.Append(UFlibPatchParserHelper::GetDefaultForceSkipContentDir());
}


TArray<FPlatformExternAssets>& FHotPatcherSettingBase::GetAddExternAssetsToPlatform()
{
	static TArray<FPlatformExternAssets> PlatformNoAssets;
	return PlatformNoAssets;
};


void FHotPatcherSettingBase::Init()
{
	
}

TArray<FExternFileInfo> FHotPatcherSettingBase::GetAllExternFilesByPlatform(ETargetPlatform InTargetPlatform,bool InGeneratedHash)
{
	TArray<FExternFileInfo> AllExternFiles = UFlibPatchParserHelper::ParserExDirectoryAsExFiles(GetAddExternDirectoryByPlatform(InTargetPlatform));
	
	for (auto& ExFile : GetAddExternFilesByPlatform(InTargetPlatform))
	{
		if (!AllExternFiles.Contains(ExFile))
		{
			AllExternFiles.Add(ExFile);
		}
	}
	if (InGeneratedHash)
	{
		for (auto& ExFile : AllExternFiles)
		{
			ExFile.GenerateFileHash();
		}
	}
	return AllExternFiles;
}
	
TMap<ETargetPlatform,FPlatformExternFiles> FHotPatcherSettingBase::GetAllPlatfotmExternFiles(bool InGeneratedHash)
{
	TMap<ETargetPlatform,FPlatformExternFiles> result;
	
	for(const auto& Platform:GetAddExternAssetsToPlatform())
	{
		FPlatformExternFiles PlatformIns{Platform.TargetPlatform,GetAllExternFilesByPlatform(Platform.TargetPlatform,InGeneratedHash)};
		result.Add(Platform.TargetPlatform,PlatformIns);
	}
	return result;
}
	
TArray<FExternFileInfo> FHotPatcherSettingBase::GetAddExternFilesByPlatform(ETargetPlatform InTargetPlatform)
{
	TArray<FExternFileInfo> result;
	for(const auto& Platform:GetAddExternAssetsToPlatform())
	{
		if (Platform.TargetPlatform == InTargetPlatform)
		{
			for(const auto& File:Platform.AddExternFileToPak)
			{
				uint32 index = result.Emplace(File);
				result[index].FilePath.FilePath = UFlibPatchParserHelper::ReplaceMarkPath(File.FilePath.FilePath);
			}
		}
	}

	return result;
}
TArray<FExternDirectoryInfo> FHotPatcherSettingBase::GetAddExternDirectoryByPlatform(ETargetPlatform InTargetPlatform)
{
	TArray<FExternDirectoryInfo> result;
	for(const auto& Platform:GetAddExternAssetsToPlatform())
	{
		if (Platform.TargetPlatform == InTargetPlatform)
		{
			for(const auto& Dir:Platform.AddExternDirectoryToPak)
			{
				uint32 index = result.Emplace(Dir);
				result[index].DirectoryPath.Path = UFlibPatchParserHelper::ReplaceMarkPath(Dir.DirectoryPath.Path);
			}
		}
	}

	return result;
}

FString FHotPatcherSettingBase::GetSaveAbsPath()const
{
	if (!SavePath.Path.IsEmpty())
	{
		return UFlibPatchParserHelper::ReplaceMarkPath(SavePath.Path);
	}
	return TEXT("");
}

TArray<FString> FHotPatcherSettingBase::GetAllSkipContents() const

{
	TArray<FString> AllSkipContents;;
	AllSkipContents.Append(UFlibAssetManageHelper::DirectoryPathsToStrings(GetForceSkipContentRules()));
	AllSkipContents.Append(UFlibAssetManageHelper::SoftObjectPathsToStrings(GetForceSkipAssets()));
	return AllSkipContents;
}

