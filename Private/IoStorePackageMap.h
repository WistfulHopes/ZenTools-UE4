// Copyright Nikita Zolotukhin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/IoContainerId.h"
#include "IO/IoDispatcher.h"
#include "Serialization/AsyncLoading2.h"

/** Metadata about a single package container */
struct FPackageContainerMetadata
{
	TArray<FPackageId> PackagesInContainer;
	TArray<FPackageId> OptionalPackagesInContainer;
};

/** Data about the package header located in the Container Header, needed to parse the exports */
struct FPackageHeaderData
{
	TArray<FPackageId> ImportedPackages;
	int32 ExportCount{0};
	int32 ExportBundleCount{0};
};

/** Package map entry for the Script Object */
struct FPackageMapScriptObjectEntry
{
	/** Name of this Object. Not a full path! */
	FName ObjectName;
	/** The package object index used to refer to this object. Will always be a ScriptImport */
	FPackageObjectIndex ScriptObjectIndex{};
	/** Index of the Outer of this script object */
	FPackageObjectIndex OuterIndex{};
	/** Index of the class this CDO is of if this import is a CDO object */
	FPackageObjectIndex CDOClassIndex{};
};

/** Import entry describes a Script package name or a PackageId and ExportHash used to uniquely identify an exported object */
struct FPackageMapImportEntry
{
	/** Index to use in the global lookup map to find a script object */
	FPackageObjectIndex ScriptImportIndex{};
	FPackageObjectIndex GlobalImportIndex{};
	/** True if this is a script import */
	bool bIsScriptImport{false};
	/** True if this is a Null import, in that case nothing else will be set */
	bool bIsNullImport{false};
	/** True if this is a package import */
	bool bIsPackageImport{false};
	bool operator==(const FPackageMapImportEntry& other) const;
};

inline bool FPackageMapImportEntry::operator==(const FPackageMapImportEntry& other) const
{
	return ScriptImportIndex == other.ScriptImportIndex && GlobalImportIndex == other.GlobalImportIndex
		&& bIsScriptImport == other.bIsScriptImport && bIsNullImport == other.bIsNullImport
		&& bIsPackageImport == other.bIsPackageImport;
}

/** Represents an object reference inside of the package, can be either an import or index into the export map */
struct FPackageLocalObjectRef
{
	/** If this represents an import, the entry describing it */
	FPackageMapImportEntry Import;
	/** If this represents an export, an index into the Exports of the package this ref belongs to */
	uint32 ExportIndex{0};
	/** True if this reference is an index into the exports objects of this package and not an external import */
	bool bIsExportReference{false};
	/** True if this is an import from another package or script */
	bool bIsImport{false};
	/** True if this is Null, which means this is a top level export */
	bool bIsNull{false};
	bool operator==(const FPackageLocalObjectRef& other) const;
};

inline bool FPackageLocalObjectRef::operator==(const FPackageLocalObjectRef& other) const
{
	return Import == other.Import && ExportIndex == other.ExportIndex && bIsExportReference == other.bIsExportReference
		&& bIsImport == other.bIsImport && bIsNull == other.bIsNull;
}

/** Export entry describes a single exported object inside of the export bundle */
struct FPackageMapExportEntry
{
	/** Name of the exported object */
	FName ObjectName;
	FName FullName;
	/** Indices of the various objects related to this export */
	FPackageLocalObjectRef OuterIndex;
	FPackageLocalObjectRef ClassIndex;
	FPackageLocalObjectRef SuperIndex;
	FPackageLocalObjectRef TemplateIndex;
	FPackageLocalObjectRef GlobalImportIndex;
	/** Flags set on the object */
	EObjectFlags ObjectFlags{RF_NoFlags};
	/** Flags to filter the export out on the client or server */
	EExportFilterFlags FilterFlags{};
	/** Offset of the serial data from the start of the chunk */
	int32 SerialDataOffset{0};
	/** Size of the serialized cooked data */
	int32 SerialDataSize{0};
	/** Serialized cooked data blob for this export */
	TSharedPtr<TArray<uint8>> CookedSerialData;
};

struct FArc
{
	uint32 FromNodeIndex;
	uint32 ToNodeIndex;

	bool operator==(const FArc& Other) const
	{
		return ToNodeIndex == Other.ToNodeIndex && FromNodeIndex == Other.FromNodeIndex;
	}
	
	friend FArchive& operator<<(FArchive& Ar, FArc& A)
	{
		Ar << A.FromNodeIndex;
		Ar << A.ToNodeIndex;

		return Ar;
	}
};

/** Describes a dependency on the external package's export bundle */
struct FPackageMapExternalDependencyArc
{
	FPackageId ImportedPackageId{};
	int32 ExternalArcCount;
	TArray<FArc> Arcs;
};

/**
 * Describes an export bundle, e.g. a package inside of the IO store container.
 * This intentionally omits some zen-specific data like arcs and export bundle entries, because they are generated during the packaging time and
 * serve no purpose other than optimizing the performance of the zen loader in runtime.
 */
struct FPackageMapExportBundleEntry
{
	FName PackageName;
	/** If present, versioning info staged inside of the export bundle. Usually absent if -unversioned is provided */
	// TOptional<FZenPackageVersioningInfo> VersioningInfo;
	/** Flags of the UPackage object this describes */
	uint32 PackageFlags{PKG_None};
	/** Package name map */
	TArray<FName> NameMap;
	/** Processed map of the imported objects from other packages and script objects */
	TArray<FPackageMapImportEntry> ImportMap;
	/** Processed map of the exported objects inside of this package */
	TArray<FPackageMapExportEntry> ExportMap;
	/** Export bundles for this package */
	TArray<TArray<FExportBundleEntry>> ExportBundles;
	/** Dependencies from the package bundles inside of this package to external packages */
	TArray<FPackageMapExternalDependencyArc> ExternalArcs;
	/** ID of the chunk in which exports of this package are located */
	FIoChunkId PackageChunkId;
	/** ID of the bulk data chunks for this package */
	TArray<FIoChunkId> BulkDataChunkIds;
	uint32 CookedHeaderSize = 0;
	FPackageId PackageId;
};

/** Package map is a central storage mapping package IDs (and overall any FPackageObjectIndex objects) to their names and locations */
class ZENTOOLS_API FIoStorePackageMap
{
private:
	TMap<FPackageId, FPackageHeaderData> PackageHeaders;
	TMap<FPackageObjectIndex, FPackageMapScriptObjectEntry> ScriptObjectMap;
	TMap<FPackageId, FPackageMapExportBundleEntry> PackageMap;
	TArray<FPackageMapExportBundleEntry> PackageInfos;
	TMap<FPackageObjectIndex, int32> ExportIndices;
	TMap<FIoContainerId, FPackageContainerMetadata> ContainerMetadata;
public:

	/** Salvages the provided IoStore container for the exports and script objects and populates the map */
	void PopulateFromContainer(const TSharedPtr<FIoStoreReader>& Reader);

	/** Attempts to find a script object in the map, returns true and the info if it was found */
	bool FindScriptObject( const FPackageObjectIndex& Index, FPackageMapScriptObjectEntry& OutMapEntry ) const;

	/** Attempts to find the export bundle for the given package */
	bool FindExportBundleData( const FPackageId& PackageId, FPackageMapExportBundleEntry& OutExportBundleEntry ) const;

	/** Attempts to find the export bundle for the given package */
	bool FindExportBundleData( const FPackageObjectIndex& Index, FPackageMapExportBundleEntry& OutExportBundleEntry ) const;

	int32 FindExportIndex(const FPackageObjectIndex& Index);
	
	bool FindPackageContainerMetadata( FIoContainerId ContainerId, FPackageContainerMetadata& OutMetadata ) const;

	bool FindPackageHeader( const FPackageId& PackageId, FPackageHeaderData& OutPackageHeader ) const;

	FName FindPackageName( const FPackageId& PackageId ) const;
  
	FORCEINLINE int32 GetTotalPackageCount() const { return PackageInfos.Num(); }

	static FPackageLocalObjectRef ResolvePackageLocalRef(const FPackageObjectIndex& PackageObjectIndex);
private:
	void ReadScriptObjects( const FIoBuffer& ChunkBuffer, const FIoBuffer& NamesIoBuffer, const FIoBuffer& NamesHashesIoBuffer );
	FPackageMapExportBundleEntry* ReadExportBundleData( const FPackageId& PackageId, const FIoStoreTocChunkInfo& ChunkInfo, const FIoBuffer& ChunkBuffer );
};
