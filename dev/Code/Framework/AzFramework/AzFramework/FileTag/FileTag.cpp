/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <AzCore/Serialization/Utils.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/string/wildcard.h>
#include <AzCore/std/string/regex.h>
#include <AzCore/std/string/conversions.h>
#include <AzCore/XML/rapidxml.h>
#include <AzFramework/API/ApplicationAPI.h>
#include <AzFramework/Asset/FileTagAsset.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzFramework/FileTag/FileTag.h>
#include <cctype>

namespace AzFramework
{
    namespace FileTag
    {
        const char* BlackListFileName = "blacklist";
        const char* WhiteListFileName = "whitelist";
        const char* FileTags[] = { "ignore", "error", "productdependency", "editoronly", "shader" };
        const char EngineName[] = "Engine";

        void LowerCaseFileTags(AZStd::vector<AZStd::string>& fileTags)
        {
            AZStd::transform(fileTags.begin(), fileTags.end(), fileTags.begin(),
                [](AZStd::string& fileTag) {
                AZStd::to_lower(fileTag.begin(), fileTag.end());
                return fileTag;
            });
        }

        void LowerCaseFileTags(AZStd::set<AZStd::string>& fileTags)
        {
            AZStd::transform(fileTags.begin(), fileTags.end(), fileTags.begin(),
                [](AZStd::string& fileTag) {
                AZStd::to_lower(fileTag.begin(), fileTag.end());
                return fileTag;
            });
        }

        bool NormalizeFileAndLowerCaseTags(AZStd::string& filePath, AzFramework::FileTag::FilePatternType filePatternType, AZStd::vector<AZStd::string>& fileTags)
        {
            if (filePatternType == AzFramework::FileTag::FilePatternType::Exact)
            {
                // if we are here it means this is a file and we would have to normalize the path
                if (!AzFramework::StringFunc::AssetDatabasePath::Normalize(filePath))
                {
                    return false;
                }
            }

            LowerCaseFileTags(fileTags);
            return true;
        }

        FileTagManager::FileTagManager()
        {
            FileTagsEventBus::Handler::BusConnect();
        }

        FileTagManager::~FileTagManager()
        {
            FileTagsEventBus::Handler::BusDisconnect();
        }

        bool FileTagManager::Save(FileTagType fileTagType, const AZStd::string& destinationFilePath = AZStd::string())
        {
            AzFramework::FileTag::FileTagAsset* fileTagAsset = GetFileTagAsset(fileTagType);
            AZStd::string filePathToSave = destinationFilePath;
            if (filePathToSave.empty())
            {
                filePathToSave = FileTagQueryManager::GetDefaultFileTagFilePath(fileTagType);
            }

            if (!AzFramework::StringFunc::EndsWith(filePathToSave, AzFramework::FileTag::FileTagAsset::Extension()))
            {
                AZ_Error("FileTag", false, "Unable to save tag file (%s). Invalid file extension, file tag can only have (%s) extension.\n", filePathToSave.c_str(), AzFramework::FileTag::FileTagAsset::Extension());
                return false;
            }

            return AZ::Utils::SaveObjectToFile(filePathToSave, AZ::DataStream::StreamType::ST_XML, fileTagAsset);
        }

        AZ::Outcome<AZStd::string, AZStd::string> FileTagManager::AddTagsInternal(AZStd::string filePath, FileTagType fileTagType, AZStd::vector<AZStd::string> fileTags, AzFramework::FileTag::FilePatternType filePatternType)
        {   
            if (!NormalizeFileAndLowerCaseTags(filePath, filePatternType, fileTags))
            {
                return AZ::Failure(AZStd::string::format("Unable to normalize file (%s). Unable to remove the file.\n", filePath.c_str()));
            }

            AzFramework::FileTag::FileTagAsset* fileTagAsset = GetFileTagAsset(fileTagType);
            AZStd::string successString;

            auto entryFound = fileTagAsset->m_fileTagMap.find(filePath);
            if (entryFound != fileTagAsset->m_fileTagMap.end())
            {
                if (filePatternType != AzFramework::FileTag::FilePatternType::Exact && filePatternType != entryFound->second.m_filePatternType)
                {
                    return AZ::Failure(AZStd::string::format("Pattern (%s) patternType (%d) does not match. Expected pattern type (%d)).\n", filePath.c_str(), static_cast<unsigned int>(filePatternType), static_cast<unsigned int>(entryFound->second.m_filePatternType)));
                }
                for (const AZStd::string& fileTag : fileTags)
                {
                    auto tagFound = entryFound->second.m_fileTags.find(fileTag);
                    if (tagFound == entryFound->second.m_fileTags.end())
                    {
                        // if we are here it means that we need to add this fileTag
                        entryFound->second.m_fileTags.insert(fileTag);
                    }
                    else
                    {
                        successString.append(AZStd::string::format("File tag (%s) already present for file/pattern (%s).\n", fileTag.c_str(), filePath.c_str()));
                    }
                }
            }
            else
            {
                // if we are here we need to add a new entry in the map
                AZStd::set<AZStd::string> tags(fileTags.begin(), fileTags.end());
                AzFramework::FileTag::FileTagData fileTagData(tags, filePatternType);
                fileTagAsset->m_fileTagMap.insert(AZStd::pair<AZStd::string, AzFramework::FileTag::FileTagData>(filePath, fileTagData));
            }

            return AZ::Success(successString);
        }

        AZ::Outcome<AZStd::string, AZStd::string> FileTagManager::RemoveTagsInternal(AZStd::string filePath, FileTagType fileTagType, AZStd::vector<AZStd::string> fileTags, AzFramework::FileTag::FilePatternType filePatternType)
        {
            if (!NormalizeFileAndLowerCaseTags(filePath, filePatternType, fileTags))
            {
                return AZ::Failure(AZStd::string::format("Unable to normalize file (%s). Unable to remove the file.\n", filePath.c_str()));
            }

            AzFramework::FileTag::FileTagAsset* fileTagAsset = GetFileTagAsset(fileTagType);
            AZStd::string successString;

            auto entryFound = fileTagAsset->m_fileTagMap.find(filePath);
            if (entryFound == fileTagAsset->m_fileTagMap.end())
            {
                return AZ::Failure(AZStd::string::format("Unable to find file/pattern (%s) for removal.\n", filePath.c_str()));
            }

            if (filePatternType != AzFramework::FileTag::FilePatternType::Exact && filePatternType != entryFound->second.m_filePatternType)
            {
                return AZ::Failure(AZStd::string::format("Pattern (%s) patternType (%d) does not match. Expected pattern type is (%d)).\n", filePath.c_str(), static_cast<unsigned int>(filePatternType), static_cast<unsigned int>(entryFound->second.m_filePatternType)));
            }

            for (AZStd::string& fileTag : fileTags)
            {
                auto tagFound = entryFound->second.m_fileTags.find(fileTag);
                if (tagFound != entryFound->second.m_fileTags.end())
                {
                    // if we are here it means that we need to remove this fileTag
                    entryFound->second.m_fileTags.erase(fileTag);
                }
                else
                {
                    successString.append(AZStd::string::format("File tag (%s) does not exist for file/pattern (%s).\n", fileTag.c_str(), filePath.c_str()));
                }
            }

            if (!entryFound->second.m_fileTags.size())
            {
                fileTagAsset->m_fileTagMap.erase(filePath);
            }

            return AZ::Success(successString);
        }

        AZ::Outcome<AZStd::string, AZStd::string> FileTagManager::AddFileTags(const AZStd::string& filePath, FileTagType fileTagType, const AZStd::vector<AZStd::string>& fileTags)
        {
            return AddTagsInternal(filePath, fileTagType, fileTags);
        }


        AZ::Outcome<AZStd::string, AZStd::string> FileTagManager::RemoveFileTags(const AZStd::string& filePath, FileTagType fileTagType, const AZStd::vector<AZStd::string>& fileTags)
        {
            return RemoveTagsInternal(filePath, fileTagType, fileTags);
        }

        AZ::Outcome<AZStd::string, AZStd::string> FileTagManager::AddFilePatternTags(const AZStd::string& pattern, AzFramework::FileTag::FilePatternType filePatternType, FileTagType fileTagType, const AZStd::vector<AZStd::string>& fileTags)
        {
            return AddTagsInternal(pattern, fileTagType, fileTags, filePatternType);
        }

        AZ::Outcome<AZStd::string, AZStd::string> FileTagManager::RemoveFilePatternTags(const AZStd::string& pattern, AzFramework::FileTag::FilePatternType filePatternType, FileTagType fileTagType, const AZStd::vector<AZStd::string>& fileTags)
        {
            return RemoveTagsInternal(pattern, fileTagType, fileTags, filePatternType);
        }

        AzFramework::FileTag::FileTagAsset* FileTagManager::GetFileTagAsset(FileTagType fileTagType)
        {
            auto assetFound = m_fileTagAssetsMap.find(fileTagType);
            if (assetFound == m_fileTagAssetsMap.end())
            {
                m_fileTagAssetsMap[fileTagType] = AZStd::make_unique<AzFramework::FileTag::FileTagAsset>();
            }

            return m_fileTagAssetsMap[fileTagType].get();
        }

        FileTagQueryManager::FileTagQueryManager(FileTagType fileTagType)
            :m_fileTagType(fileTagType)
        {
            QueryFileTagsEventBus::Handler::BusConnect(fileTagType);
        }

        FileTagQueryManager::~FileTagQueryManager()
        {
            QueryFileTagsEventBus::Handler::BusDisconnect();
        }

        AZStd::string FileTagQueryManager::GetDefaultFileTagFilePath(FileTagType fileTagType)
        {
            AZStd::string destinationFilePath;
            const char* appRoot = nullptr;
            AzFramework::ApplicationRequests::Bus::BroadcastResult(appRoot, &AzFramework::ApplicationRequests::GetEngineRoot);
            AzFramework::StringFunc::Path::ConstructFull(appRoot, EngineName, fileTagType == FileTagType::BlackList ? BlackListFileName : WhiteListFileName, AzFramework::FileTag::FileTagAsset::Extension(), destinationFilePath, true);
            return destinationFilePath;
        }

        bool FileTagQueryManager::Load(const AZStd::string& filePath)
        {
            AZStd::string fileToLoad = filePath;
            if (fileToLoad.empty())
            {
                fileToLoad = GetDefaultFileTagFilePath(m_fileTagType);
            }
            AZ::IO::FileIOBase* fileIO = AZ::IO::FileIOBase::GetInstance();
            AZ_Assert(fileIO != nullptr, "AZ::IO::FileIOBase must be ready for use.\n");

            if (!fileIO->Exists(fileToLoad.c_str()))
            {
                AZ_Error("AzFramework::FileTagManager", false, "File (%s) does not exist on disk. Unable to load the file.\n", fileToLoad.c_str());
                return false;
            }
            AzFramework::FileTag::FileTagAsset fileTagAsset;
            if (!AZ::Utils::LoadObjectFromFileInPlace(fileToLoad.c_str(), fileTagAsset))
            {
                AZ_Error("AzFramework::FileTagManager", false, "Unable to deserialize file (%s) from disk.\n", fileToLoad.c_str());
                return false;
            }

            for (AZStd::pair<AZStd::string, AzFramework::FileTag::FileTagData>& data : fileTagAsset.m_fileTagMap)
            {
                LowerCaseFileTags(data.second.m_fileTags);
                if (data.second.m_filePatternType != AzFramework::FileTag::FilePatternType::Exact)
                {
                    m_patternTagsMap[data.first] = data.second;
                }
                else
                {
                    m_fileTagsMap[data.first] = data.second;
                }
            }
            return true;
        }

        bool FileTagQueryManager::LoadEngineDependencies(const AZStd::string& filePath)
        {
            // Load engine dependencies specified inside *_dependencies.xml
            // There's no shared code we can use to deal with all of the loading/parsing of XML files
            // We may want to make a method in AzFramework somewhere to avoid copying the code
            AZ::IO::FileIOStream fileStream;
            if (!fileStream.Open(filePath.c_str(), AZ::IO::OpenMode::ModeRead) ||
                !fileStream.GetLength())
            {
                return false;
            }

            AZ::IO::SizeType length = fileStream.GetLength();
            AZStd::vector<char> charBuffer;
            charBuffer.resize_no_construct(length + 1);
            fileStream.Read(length, charBuffer.data());
            charBuffer.back() = 0;

            AZ::rapidxml::xml_document<char> xmlDoc;
            if (!xmlDoc.parse<AZ::rapidxml::parse_no_data_nodes>(charBuffer.data()))
            {
                return false;
            }

            AZ::rapidxml::xml_node<char>* xmlRootNode = xmlDoc.first_node();
            if (strcmp(xmlRootNode->name(), "EngineDependencies") != 0)
            {
                return false;
            }

            for (AZ::rapidxml::xml_node<char>* xmlFileChildNode = xmlRootNode->first_node(); xmlFileChildNode; xmlFileChildNode = xmlFileChildNode->next_sibling())
            {
                AZ::rapidxml::xml_attribute<char>* optionalAttr = xmlFileChildNode->first_attribute("optional", 0, false);
                if (!optionalAttr ||
                    strcmp(optionalAttr->value(), "true") != 0)
                {
                    continue;
                }

                AZ::rapidxml::xml_attribute<char>* pathAttr = xmlFileChildNode->first_attribute("path", 0, false);
                if (strcmp(xmlFileChildNode->name(), "Dependency") != 0 ||
                    !pathAttr)
                {
                    continue;
                }

                // File paths specified inside *_dependencies.xml may not have the correct file system separator (e.g. <Dependency path="config\\singleplayer.cfg" />)
                // We need to replace the wrong separator and add '*' at the beginning of the string to make it work with the tag system
                AZStd::string filePathPattern = pathAttr->value();
                AZStd::replace(filePathPattern.begin(), filePathPattern.end(), AZ_WRONG_DATABASE_SEPARATOR, AZ_CORRECT_DATABASE_SEPARATOR);
                if (!filePathPattern.starts_with("*"))
                {
                    filePathPattern = "*" + filePathPattern;
                }

                AZStd::to_lower(filePathPattern.begin(), filePathPattern.end());
                m_patternTagsMap[filePathPattern] = AzFramework::FileTag::FileTagData({ "ignore", "productdependency" }, FilePatternType::Wildcard);
            }

            return true;
        }

        bool FileTagQueryManager::Match(const AZStd::string& filePath, AZStd::vector<AZStd::string> fileTags)
        {
            AZStd::set<AZStd::string> tags = GetTags(filePath);

            LowerCaseFileTags(fileTags);

            for (const AZStd::string& tag : fileTags)
            {
                if (tags.count(tag) == 0)
                {
                    return false;
                }
            }

            return true;
        }

        AZStd::set<AZStd::string> FileTagQueryManager::GetTags(const AZStd::string& filePath)
        {
            AZStd::set<AZStd::string> tags;
            AZStd::string normalizedFilePath = filePath;
            AzFramework::StringFunc::AssetDatabasePath::Normalize(normalizedFilePath);
            auto found = m_fileTagsMap.find(normalizedFilePath);
            if (found != m_fileTagsMap.end())
            {
                tags.insert(found->second.m_fileTags.begin(), found->second.m_fileTags.end());
            }

            for (const AZStd::pair<AZStd::string, AzFramework::FileTag::FileTagData>& data : m_patternTagsMap)
            {
                if (data.second.m_filePatternType == AzFramework::FileTag::FilePatternType::Wildcard)
                {
                    if (AZStd::wildcard_match(data.first, normalizedFilePath))
                    {
                        tags.insert(data.second.m_fileTags.begin(), data.second.m_fileTags.end());
                    }
                }
                else
                {
                    AZStd::regex regex(data.first, AZStd::regex::extended);
                    if (AZStd::regex_match(normalizedFilePath.c_str(), regex))
                    {
                        tags.insert(data.second.m_fileTags.begin(), data.second.m_fileTags.end());
                    }
                }
            }

            return tags;
        }
    }
}
