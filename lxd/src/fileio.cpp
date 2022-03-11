#include "fileio.h"
#include <Windows.h>
#include <fileapi.h>
#include <pathcch.h>
#include <fmt/format.h>
#include <cassert>
#include <fmt/xchar.h>

namespace lxd {
	bool FileExists(const wchar_t* path) {
		return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
	}

	bool openModeCanCreate(int openMode) {
		// WriteOnly can create, but only when ExistingOnly isn't specified.
		// ReadOnly by itself never creates.
		return (openMode & OpenMode::WriteOnly) && !(openMode & OpenMode::ExistingOnly);
	}

	void* OpenFile(const wchar_t* path, int openMode) {
		// All files are opened in share mode (both read and write).
		DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		int accessRights = 0;
		if (openMode & OpenMode::ReadOnly)
			accessRights |= GENERIC_READ;
		if (openMode & OpenMode::WriteOnly)
			accessRights |= GENERIC_WRITE;
		// WriteOnly can create files, ReadOnly cannot.
		DWORD creationDisp = (openMode & OpenMode::NewOnly)
			? CREATE_NEW
			: openModeCanCreate(openMode)
			? OPEN_ALWAYS
			: OPEN_EXISTING;

		CREATEFILE2_EXTENDED_PARAMETERS param = {};
		param.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
		param.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		param.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
		param.dwSecurityQosFlags = SECURITY_ANONYMOUS;
		HANDLE hFile = CreateFile2(path, accessRights, shareMode, creationDisp, &param);
		// Append or Truncate
		if (openMode & OpenMode::Append) {
			LONG lFileHigh = 0;
			SetFilePointer(hFile, 0, &lFileHigh, FILE_END);
		}
		else if (openMode & OpenMode::Truncate) {
			SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
			SetEndOfFile(hFile);
		}

		return hFile;
	}

	bool CloseFile(void* handle) {
		return CloseHandle(handle);
	}

	bool WriteFile(const wchar_t* path, char const* buffer, size_t bufferSize) {
		// open the file
		auto handle = OpenFile(path, OpenMode::WriteOnly | OpenMode::Truncate);
		if (INVALID_HANDLE_VALUE == handle) {
			return false;
		}
		// get file size
		LARGE_INTEGER size;
		if (!GetFileSizeEx(handle, &size)) {
			CloseHandle(handle);
			return false;
		}
		// write
		if (!::WriteFile(handle, buffer, static_cast<DWORD>(bufferSize), nullptr, nullptr)) {
			CloseHandle(handle);
			return false;
		}
		CloseFile(handle);
		return true;
	}

	std::string ReadFile(const wchar_t* path) {
		// open the file
		auto handle = OpenFile(path, OpenMode::ReadOnly | OpenMode::ExistingOnly);
		if (INVALID_HANDLE_VALUE == handle) {
			return {};
		}
		// get file size
		LARGE_INTEGER size;
		if (!GetFileSizeEx(handle, &size)) {
			CloseHandle(handle);
			return {};
		}
		// read
		std::string buffer;
		buffer.resize(size.QuadPart);
		if (!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, nullptr)) {
			CloseHandle(handle);
			return {};
		}

		CloseFile(handle);
		return buffer;
	}

	bool RemoveFile(const wchar_t* path) {
		return DeleteFileW(path) != 0;
	}

	bool CreateDir(const wchar_t* path) {
		return ::CreateDirectoryW(path, nullptr);
	}

	bool CreateDirRecursive(const std::wstring& path) {
		if (DirExists(path)) {
			return true;
		}

		size_t i = path.find_last_of(L"\\");
		if (i == std::wstring_view::npos) {
			return false;
		}

		if (!CreateDirRecursive(path.substr(0, i))) {
			return false;
		}

		return CreateDir(path.data());
	}

	int DeleteDir(std::wstring_view path, bool bDeleteSubdirectories) {
		bool				bSubdirectory = false; // Flag, indicating whether
												   // subdirectories have been found
		HANDLE				hFile;                 // Handle to directory
		std::wstring		strFilePath;           // Filepath
		std::wstring		strPattern;            // Pattern
		WIN32_FIND_DATAW	FileInformation;       // File information


		strPattern = fmt::format(L"{0}{1}", path, L"\\*.*");
		hFile = ::FindFirstFileW(strPattern.c_str(), &FileInformation);
		if (hFile != INVALID_HANDLE_VALUE) {
			do {
				if (FileInformation.cFileName[0] != '.') {
					strFilePath.erase();
					strFilePath = fmt::format(L"{0}{1}{2}", path, L"\\", FileInformation.cFileName);

					if (FileInformation.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
						if (bDeleteSubdirectories) {
							// Delete subdirectory
							int iRC = DeleteDir(strFilePath, bDeleteSubdirectories);
							if (iRC)
								return iRC;
						}
						else
							bSubdirectory = true;
					}
					else {
						// Set file attributes
						if (::SetFileAttributesW(strFilePath.c_str(),
							FILE_ATTRIBUTE_NORMAL) == FALSE)
							return ::GetLastError();

						// Delete file
						if (::DeleteFileW(strFilePath.c_str()) == FALSE)
							return ::GetLastError();
					}
				}
			} while (::FindNextFileW(hFile, &FileInformation) == TRUE);

			// Close handle
			::FindClose(hFile);

			DWORD dwError = ::GetLastError();
			if (dwError != ERROR_NO_MORE_FILES)
				return dwError;
			else {
				if (!bSubdirectory) {
					// Set directory attributes
					if (::SetFileAttributesW(path.data(),
						FILE_ATTRIBUTE_NORMAL) == FALSE)
						return ::GetLastError();

					// Delete directory
					if (::RemoveDirectoryW(path.data()) == FALSE)
						return ::GetLastError();
				}
			}
		}

		return 0;
	}

	bool DirExists(std::wstring_view path) {
		DWORD attrib = GetFileAttributesW(path.data());
		return (attrib != INVALID_FILE_ATTRIBUTES) && (attrib & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool ListDir(std::wstring_view path, std::vector<std::wstring>& result, bool recursive, const wchar_t* filter) {
		auto searchPath = fmt::format(L"{}\\*", path);
		WIN32_FIND_DATAW FindFileData;
		HANDLE hFind = FindFirstFileW(searchPath.data(), &FindFileData);
		if (hFind == INVALID_HANDLE_VALUE) {
			return false;
		}

		static std::wstring currentPath;

		do {
			if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (!wcscmp(L".", FindFileData.cFileName) || !wcscmp(L"..", FindFileData.cFileName)) {
					continue;
				}
				if (recursive) {
					auto searchPath2 = fmt::format(L"{}\\{}", path, FindFileData.cFileName);
					currentPath = FindFileData.cFileName;
					ListDir(searchPath2.data(), result, recursive, filter);
				}
			}
			else {
				if (nullptr == filter) {
					if (currentPath.empty()) {
						result.push_back(FindFileData.cFileName);
					}
					else {
						result.push_back(fmt::format(L"{}\\{}", currentPath, FindFileData.cFileName));
					}
				}
				else {
					int lenFilter = lstrlenW(filter);
					int lenName = lstrlenW(FindFileData.cFileName);
					if (lenFilter < lenName) {
						if (lstrcmpiW(filter, FindFileData.cFileName + (lenName - lenFilter)) == 0) {
							if (currentPath.empty()) {
								result.push_back(FindFileData.cFileName);
							}
							else {
								result.push_back(fmt::format(L"{}\\{}", currentPath, FindFileData.cFileName));
							}
						}
					}
				}
			}
		} while (FindNextFileW(hFind, &FindFileData) != 0);

		currentPath.clear();
		FindClose(hFind);

		return true;
	}

	File::File(const std::wstring& path, int mode) {
		assert(path.size() > 3); // D:\\1
		auto offset = path.find_last_of(L"\\");
		if (offset != std::wstring::npos) {
			auto middleDir = path.substr(0, offset);
			CreateDirRecursive(middleDir);
		}
		_handle = OpenFile(path.data(), mode);
		assert(INVALID_HANDLE_VALUE != _handle);
		// get file size
		bool ok = GetFileSizeEx(_handle, reinterpret_cast<PLARGE_INTEGER>(&_size));
		assert(ok);
	}

	File::~File() {
		CloseFile(_handle);
	}

	bool File::seek(long long distance, SeekMode mode, long long* newPtr) {
		_LARGE_INTEGER _distance;
		_distance.QuadPart = distance;
		return SetFilePointerEx(_handle, _distance, reinterpret_cast<PLARGE_INTEGER>(newPtr), mode);
	}

	bool File::read(void* buffer, unsigned long nNumberOfBytesToRead, unsigned long* lpNumberOfBytesRead) {
		return ::ReadFile(_handle, buffer, nNumberOfBytesToRead, lpNumberOfBytesRead, nullptr);
	}

	bool File::write(const void* buffer, size_t bufferSize) {
		DWORD bytesWritten = 0;
		if (::WriteFile(_handle, buffer, static_cast<DWORD>(bufferSize), &bytesWritten, nullptr)) {
			_size += bytesWritten;
			return true;
		}
		return false;
	}

	struct tm File::getLastWriteTime() {
		FILETIME ftCreate, ftAccess, ftWrite;
		SYSTEMTIME stUTC, stLocal;

		// Retrieve the file times for the file.
		bool ok = GetFileTime(_handle, &ftCreate, &ftAccess, &ftWrite);
		assert(ok);

		// Convert the last-write time to local time.
		FileTimeToSystemTime(&ftWrite, &stUTC);
		SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

		return tm{
			stLocal.wSecond, stLocal.wMinute, stLocal.wHour,
			stLocal.wDay, stLocal.wMonth - 1, stLocal.wYear - 1900,
			stLocal.wDayOfWeek, 0, 0
		};
	}

	bool File::isOlderThan(struct tm otherTM) {
		tm lastWT = getLastWriteTime();
		return lastWT.tm_year < otherTM.tm_year ||
			lastWT.tm_mon < otherTM.tm_mon ||
			lastWT.tm_mday < otherTM.tm_mday ||
			lastWT.tm_hour < otherTM.tm_hour ||
			lastWT.tm_min < otherTM.tm_min ||
			lastWT.tm_sec < otherTM.tm_sec;
	}

}