// Copyright 2005-2019 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ProcessResolver.h"
#include <cstring>

ProcessResolver::ProcessResolver(bool resolveImmediately) : processNames(), processPIDs() {
	if (resolveImmediately) {
		this->resolve();
	}
}

ProcessResolver::~ProcessResolver() {
	this->freeAndClearData();
}

void ProcessResolver::freeAndClearData() {
	// delete all names
	foreach(const char *currentName, this->processNames) {
		delete currentName;
	}

	this->processNames.clear();
	this->processPIDs.clear();
}

const QVector<const char*>& ProcessResolver::getProcessNames() const {
	return this->processNames;
}

const QVector<uint64_t>& ProcessResolver::getProcessPIDs() const {
	return this->processPIDs;
}

void ProcessResolver::resolve() {
	// first clear the current lists
	this->freeAndClearData();

	this->doResolve();
}


/// Helper function to add a name stored as a stack-variable to the given vector
///
/// @param stackName The pointer to the stack-variable
/// @param destVec The destination vector to add the pointer to
void addName(const char *stackName, QVector<const char*>& destVec) {
	// We can't store the pointer of a stack-variable (will be invalid as soon as we exit scope)
	// so we'll have to allocate memory on the heap and copy the name there.
	size_t nameLength = std::strlen(stackName) + 1; // +1 for terminating NULL-byte
	char *name = new char[nameLength];

	std::strcpy(name, stackName);

	destVec.append(name);
}

// The implementation of the doResolve-function is platfrom-dependent
// The different implementations are heavily inspired by the ones given at https://github.com/davidebeatrici/list-processes
#ifdef Q_OS_WIN
	// Implementation for Windows
	#ifndef UNICODE
		#define UNICODE
	#endif

	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif

	#include <windows.h>
	#include <tlhelp32.h>
	#include <limits>

	bool utf16ToUtf8(const wchar_t *source, const int size, char *destination) {
		if (!WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, size, NULL, NULL)) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: WideCharToMultiByte() failed with error %d\n", GetLastError());
#endif
			return false;
		}

		return true;
	}

	void ProcessResolver::doResolve() {
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnap == INVALID_HANDLE_VALUE) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: CreateToolhelp32Snapshot() failed with error %d", GetLastError());
#endif
			return;
		}

		PROCESSENTRY32 pe;
		pe.dwSize = sizeof(pe);

		BOOL ok = Process32First(hSnap, &pe);
		if (!ok) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: Process32First() failed with error %d\n", GetLastError());
#endif
			return;
		}

		char name[MAX_PATH];

		while (ok) {
			if (utf16ToUtf8(pe.szExeFile, sizeof(name), name)) {
				// Store name
				addName(name, this->processNames);

				// Store corresponding PID
				this->processPIDs.append(pe.th32ProcessID);
			}
#ifndef QT_NO_DEBUG
			 else {
				qQWarning("ProcessResolver: utf16ToUtf8() failed, skipping entry...");
			}
#endif

			ok = Process32Next(hSnap, &pe);
		}

		CloseHandle(hSnap);
	}
#elif defined(Q_OS_LINUX)
	// Implementation for Linux
	#include <cstdlib>

	#include <dirent.h>
#ifndef QT_NO_DEBUG
	#include <errno.h>
#endif
	#include <libgen.h>
	#include <linux/limits.h>
	#include <sys/stat.h>

	#define PROC_DIR "/proc/"
	#define EXE_LINK "/exe"

	bool getlinkedpath(const char *linkpath, char *linkedpath) {
		if (!realpath(linkpath, linkedpath)) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolve: realpath() failed with error %d\n", errno);
#endif
			return false;
		}

		return true;
	}

	void ProcessResolver::doResolve() {
		DIR *dir = opendir(PROC_DIR);
		if (dir == NULL) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: opendir() failed with error %d", errno);
#endif
			return;
		}

		struct dirent *de;
		while ((de = readdir(dir))) {
			// The name of the "dir" represents the PID of the process
			uint64_t pid;
			try {
				pid = std::stoull(de->d_name);
			} catch(const std::invalid_argument& e) {
				// if the name can't be converted to a PID, we will ignore this process
				Q_UNUSED(e);

				continue;
			}

			char exelinkpath[PATH_MAX];
			snprintf(exelinkpath, sizeof(exelinkpath), "%s%s%s", PROC_DIR, de->d_name, EXE_LINK);

			struct stat st;
			if (stat(exelinkpath, &st) == -1) {
				// Either the file doesn't exist or it's not accessible.
				continue;
			}

			char path[PATH_MAX];
			if (!getlinkedpath(exelinkpath, path)) {
#ifndef QT_NO_DEBUG
				qWarning("ProcessResolver: getlinkedpath() failed, skipping entry...");
#endif
				continue;
			}

			const char *programName = basename(path);
			
			// add name
			addName(programName, this->processNames);

			// add corresponding PID
			this->processPIDs.append(pid);
		}

		closedir(dir);
	}
#elif defined(Q_OS_MACOS)
	// Implementation for MacOS
	// Code taken from https://stackoverflow.com/questions/49506579/how-to-find-the-pid-of-any-process-in-mac-osx-c
	#include <libproc.h>

	void ProcessResolver::doResolve() {
		pid_t pids[2048];
		int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
		int n_proc = bytes / sizeof(pids[0]);
		for (int i = 0; i < n_proc; i++) {
			struct proc_bsdinfo proc;
			int st = proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0,
								 &proc, PROC_PIDTBSDINFO_SIZE);
			if (st == PROC_PIDTBSDINFO_SIZE) {
				// add name
				addName(proc.pbi_name, this->processNames);

				// add corresponding PID
				this->processPIDs.append(pids[i]);
			}       
		}
	}
#elif defined(Q_OS_FREEBSD)
	// Implementation for FreeBSD
	#include <libutil.h>
	#include <sys/types.h>
	#include <sys/user.h>

	void ProcessResolver::doResolve() {
		int n_procs;
		struct kinfo_proc *procs_info = kinfo_getallproc(&n_procs);
		if (!procs_info) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: kinfo_getallproc() failed\n");
#endif
			return;
		}

		for (int i = 0; i < n_procs; ++i) {
			// Add name
			addName(procs_info[i].ki_comm, this->processNames);

			// Add corresponding PID
			this->processPIDs.append(procs_info[i].ki_pid);
		}

		free(procs_info);
	}
#elif defined(Q_OS_BSD4)
	// Implementation of generic BSD other than FreeBSD
	#include <limits.h>

	#include <fcntl.h>
	#include <kvm.h>
	#include <sys/sysctl.h>
	#include <sys/user.h>

	bool kvm_cleanup(kvm_t *kd) {
		if (kvm_close(kd) == -1) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: kvm_close() failed with error %d\n", errno);
#endif
			return false;
		}

		return true;
	}

	void ProcessResolver::doResolve() {
		char error[_POSIX2_LINE_MAX];
		kvm_t *kd = kvm_open2(NULL, NULL, O_RDONLY, error, NULL);
		if (!kd) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: kvm_open2() failed with error: %s\n", error);
#endif
			return;
		}

		int n_procs;
		struct kinfo_proc *procs_info = kvm_getprocs(kd, KERN_PROC_PROC, 0, &n_procs);
		if (!procs_info) {
#ifndef QT_NO_DEBUG
			qCritical("ProcessResolver: kvm_getprocs() failed\n");
#endif
			kvm_cleanup(kd);

			return;
		}

		for (int i = 0; i < n_procs; ++i) {
			// Add name
			addName(procs_info[i].ki_comm, this->processNames);

			// Add corresponding PIDs
			this->processPIDs.append(procs_info[i].ki_pid);
		}

		kvm_cleanup(kd);
	}
#else
	#error "No implementation of ProcessResolver::resolve() available for this operating system"
#endif
