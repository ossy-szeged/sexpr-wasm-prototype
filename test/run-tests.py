#!/usr/bin/env python
#
# Copyright 2016 WebAssembly Community Group participants
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import argparse
import difflib
import fnmatch
import multiprocessing
import os
import Queue
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import find_exe
import findtests
from utils import Error


IS_WINDOWS = sys.platform == 'win32'
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT_DIR = os.path.dirname(SCRIPT_DIR)
ROUNDTRIP_PY = os.path.join(SCRIPT_DIR, 'run-roundtrip.py')
DEFAULT_TIMEOUT = 10 # seconds
SLOW_TIMEOUT_MULTIPLIER = 2


# default configurations for tests
TOOLS = {
  'sexpr-wasm': {
    'EXE': '%(sexpr-wasm)s'
  },
  'run-d8': {
    'EXE': 'test/run-d8.py',
    'FLAGS': '-e %(sexpr-wasm)s --d8-executable=%(d8)s'
  },
  'run-d8-spec': {
    'EXE': 'test/run-d8.py',
    'FLAGS': '-e %(sexpr-wasm)s --d8-executable=%(d8)s --spec'
  },
  'run-roundtrip': {
    'EXE': 'test/run-roundtrip.py',
    'FLAGS': '-v -e %(sexpr-wasm)s --wasm-wast-executable=%(wasm-wast)s'
  }
}


def Indent(s, spaces):
  return ''.join(' '*spaces + l for l in s.splitlines(1))


def DiffLines(expected, actual):
  expected_lines = expected.splitlines(1)
  actual_lines = actual.splitlines(1)
  return list(difflib.unified_diff(expected_lines, actual_lines,
                                   fromfile='expected', tofile='actual'))


def AppendBeforeExt(file_path, suffix):
  file_path_noext, ext = os.path.splitext(file_path)
  return file_path_noext + suffix + ext


def RunCommandWithTimeout(command, cwd, timeout):
  process = None
  # Cheesy way to be able to set is_timeout from inside KillProcess
  is_timeout = [False]
  def KillProcess(timeout=True):
    if process:
      try:
        if IS_WINDOWS:
          # http://stackoverflow.com/a/10830753: deleting child processes in
          # Windows
          subprocess.call(['taskkill', '/F', '/T', '/PID', str(process.pid)])
        else:
          os.killpg(os.getpgid(process.pid), 15)
      except OSError:
        pass
    is_timeout[0] = timeout

  try:
    start_time = time.time()
    kwargs = {}
    if not IS_WINDOWS:
      kwargs['preexec_fn'] = os.setsid

    # http://stackoverflow.com/a/10012262: subprocess with a timeout
    # http://stackoverflow.com/a/22582602: kill subprocess and children
    process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE,
                                                 stderr=subprocess.PIPE,
                               universal_newlines=True,
                               **kwargs)
    timer = threading.Timer(timeout, KillProcess)
    try:
      timer.start()
      stdout, stderr = process.communicate()
    finally:
      returncode = process.returncode
      process = None
      timer.cancel()
    if is_timeout[0]:
      raise Error('TIMEOUT\nSTDOUT:\n%s\nSTDERR:\n%s\n' % (stdout, stderr))
    duration = time.time() - start_time
  except OSError as e:
    raise Error(str(e))
  finally:
    KillProcess(False)

  return stdout, stderr, returncode, duration



class TestInfo(object):
  def __init__(self):
    self.name = ''
    self.generated_input_filename = ''
    self.filename = ''
    self.header = []
    self.input_filename = ''
    self.input_ = []
    self.expected_stdout = ''
    self.expected_stderr = ''
    self.tool = None
    self.exe = '%(sexpr-wasm)s'
    self.flags = []
    self.expected_error = 0
    self.slow = False
    self.skip = False
    self.is_roundtrip = False

  def CreateRoundtripInfo(self):
    result = TestInfo()
    result.name = '%s (roundtrip)' % self.name
    result.generated_input_filename = AppendBeforeExt(self.name, '-roundtrip')
    result.filename = self.filename
    result.header = self.header
    result.input_filename = self.input_filename
    result.input_ = self.input_
    result.expected_stdout = ''
    result.expected_stderr = ''
    result.tool = 'run-roundtrip'
    result.exe = ROUNDTRIP_PY
    result.flags = ['-e', '%(sexpr-wasm)s', '--wasm-wast', '%(wasm-wast)s',
                    '-v']
    result.expected_error = 0
    result.slow = self.slow
    result.skip = self.skip
    result.is_roundtrip = True
    return result

  def ShouldCreateRoundtrip(self):
    return self.tool != 'run-roundtrip'

  def ParseDirective(self, key, value):
    if key == 'EXE':
      self.exe = value
    elif key == 'STDIN_FILE':
      self.input_filename = value
      self.generated_input_filename = value
    elif key == 'FLAGS':
      self.flags += shlex.split(value)
    elif key == 'ERROR':
      self.expected_error = int(value)
    elif key == 'SLOW':
      self.slow = True
    elif key == 'SKIP':
      self.skip = True
    elif key in ['TODO', 'NOTE']:
      pass
    elif key == 'TOOL':
      if not value in TOOLS:
        raise Error('Unknown tool: %s' % value)
      self.tool = value
      for tool_key, tool_value in TOOLS[value].iteritems():
        self.ParseDirective(tool_key, tool_value)
    else:
      raise Error('Unknown directive: %s' % key)

  def Parse(self, filename):
    self.name = filename
    self.filename = os.path.join(SCRIPT_DIR, filename)
    self.generated_input_filename = self.name

    with open(self.filename) as f:
      seen_keys = set()
      state = 'header'
      empty = True
      header_lines = []
      input_lines = []
      stdout_lines = []
      stderr_lines = []
      for line in f.readlines():
        empty = False
        m = re.match(r'\s*\(;; (STDOUT|STDERR) ;;;$', line)
        if m:
          directive = m.group(1)
          if directive == 'STDERR':
            state = 'stderr'
            continue
          elif directive == 'STDOUT':
            state = 'stdout'
            continue
        else:
          m = re.match(r'\s*;;;(.*)$', line)
          if m:
            directive = m.group(1).strip()
            if state == 'header':
              key, value = directive.split(':', 1)
              key = key.strip()
              value = value.strip()
              if key in seen_keys:
                raise Error('%s already set' % key)
              seen_keys.add(key)
              self.ParseDirective(key, value)
            elif state in ('stdout', 'stderr'):
              if not re.match(r'%s ;;\)$' % state.upper(), directive):
                raise Error('Bad directive in %s block: %s' % (
                    state, directive))
              state = 'none'
            else:
              raise Error('Unexpected directive: %s' % directive)
          elif state == 'header':
            state = 'input'

        if state == 'header':
          header_lines.append(line)
        if state == 'input':
          if self.input_filename:
            raise Error('Can\'t have STDIN_FILE and input')
          input_lines.append(line)
        elif state == 'stderr':
          stderr_lines.append(line)
        elif state == 'stdout':
          stdout_lines.append(line)
    if empty:
      raise Error('empty test file')

    self.header = ''.join(header_lines)
    self.input_ = ''.join(input_lines)
    self.expected_stdout = ''.join(stdout_lines)
    self.expected_stderr = ''.join(stderr_lines)

  def GetExecutable(self):
    if os.path.splitext(self.exe)[1] == '.py':
      return [sys.executable, os.path.join(REPO_ROOT_DIR, self.exe)]
    else:
      return [self.exe]

  def FormatCommand(self, cmd, variables):
    return [arg % variables for arg in cmd]

  def GetCommand(self, filename, variables, extra_args=None):
    cmd = self.GetExecutable()
    if extra_args:
      cmd += extra_args
    cmd += self.flags
    cmd += [filename.replace(os.path.sep, '/')]
    cmd = self.FormatCommand(cmd, variables)
    return cmd

  def CreateInputFile(self, temp_dir):
    file_path = os.path.join(temp_dir, self.generated_input_filename)
    file_dir = os.path.dirname(file_path)
    try:
      os.makedirs(file_dir)
    except OSError as e:
      if not os.path.isdir(file_dir):
        raise

    file_ = open(file_path, 'wb')
    # add an empty line for each header line so the line numbers match
    if self.input_filename:
      with open(self.input_filename, 'rb') as input_filename:
        file_.write(input_filename.read())
    else:
      file_.write('\n' * self.header.count('\n'))
      file_.write(self.input_)
    file_.flush()
    file_.close()

    # make filenames relative to temp_dir so the autogenerated name is not
    # included in the error output.
    return os.path.relpath(file_.name, temp_dir)

  def Rebase(self, stdout, stderr):
    with open(self.filename, 'wb') as f:
      f.write(self.header)
      f.write(self.input_)
      if stderr:
        f.write('(;; STDERR ;;;\n')
        f.write(stderr)
        f.write(';;; STDERR ;;)\n')
      if stdout:
        f.write('(;; STDOUT ;;;\n')
        f.write(stdout)
        f.write(';;; STDOUT ;;)\n')

  def Diff(self, stdout, stderr):
    msg = ''
    if self.expected_stderr != stderr:
      diff_lines = DiffLines(self.expected_stderr, stderr)
      msg += 'STDERR MISMATCH:\n' + ''.join(diff_lines)

    if self.expected_stdout != stdout:
      diff_lines = DiffLines(self.expected_stdout, stdout)
      msg += 'STDOUT MISMATCH:\n' + ''.join(diff_lines)

    if msg:
      raise Error(msg)


class Status(object):
  def __init__(self, verbose):
    self.verbose = verbose
    self.start_time = None
    self.last_length = 0
    self.last_finished = None
    self.skipped = 0
    self.passed = 0
    self.failed = 0
    self.total = 0
    self.failed_tests = []

  def Start(self, total):
    self.total = total
    self.start_time = time.time()

  def Passed(self, info, duration):
    self.passed += 1
    if self.verbose:
      sys.stderr.write('+ %s (%.3fs)\n' % (info.name, duration))
    else:
      self.Clear()
      self._PrintShortStatus(info)
      sys.stderr.flush()

  def Failed(self, info, error_msg):
    self.failed += 1
    self.failed_tests.append(info)
    self.Clear()
    sys.stderr.write('- %s\n%s\n' % (info.name, Indent(error_msg, 2)))

  def Skipped(self, info):
    self.skipped += 1
    if self.verbose:
      sys.stderr.write('. %s (skipped)\n' % info.name)

  def UpdateTimer(self):
    self._PrintShortStatus(self.last_finished)

  def Print(self):
    self._PrintShortStatus(None)
    sys.stderr.write('\n')

  def _PrintShortStatus(self, info):
    total_duration = time.time() - self.start_time
    name = info.name if info else ''
    if (self.total - self.skipped):
      percent = 100 * (self.passed + self.failed) / (self.total - self.skipped)
    else:
      percent = 100
    status = '[+%d|-%d|%%%d] (%.2fs) %s\r' % (self.passed, self.failed,
                                              percent, total_duration, name)
    self.last_length = len(status)
    self.last_finished = info
    sys.stderr.write(status)

  def Clear(self):
    if not self.verbose:
      sys.stderr.write('%s\r' % (' ' * self.last_length))


def GetAllTestInfo(test_names, status):
  infos = []
  for test_name in test_names:
    info = TestInfo()
    try:
      info.Parse(test_name)
      infos.append(info)
    except Error as e:
      status.Failed(info, str(e))

  return infos


def ProcessWorker(i, options, variables, inq, outq, temp_dir):
  try:
    while True:
      try:
        info = inq.get(False)
        timeout = options.timeout
        if info.slow:
          timeout *= SLOW_TIMEOUT_MULTIPLIER

        try:
          rel_file_path = info.CreateInputFile(temp_dir)
          cmd = info.GetCommand(rel_file_path, variables, options.arg)
          out = RunCommandWithTimeout(cmd, temp_dir, timeout)
        except Exception as e:
          outq.put((info, e))
          continue
        outq.put((info, out))
      except Queue.Empty:
        # Seems this can be fired even when the queue isn't actually empty.
        # Double-check, via inq.empty()
        if inq.empty():
          break
  except KeyboardInterrupt:
    pass



def HandleTestResult(status, info, result, rebase=False):
  try:
    if isinstance(result, Exception):
      raise result

    stdout, stderr, returncode, duration = result
    if info.is_roundtrip:
      if returncode == 0:
        status.Passed(info, duration)
      elif returncode == 2:
        # run-roundtrip.py returns 2 if the file couldn't be parsed.
        # it's likely a "bad-*" file.
        status.Skipped(info)
      else:
        raise Error(stderr)
    else:
      if returncode != info.expected_error:
        # This test has already failed, but diff it anyway.
        msg = 'expected error code %d, got %d.' % (info.expected_error,
                                                   returncode)
        try:
          info.Diff(stdout, stderr)
        except Error as e:
          msg += '\n' + str(e)
        raise Error(msg)
      else:
        if rebase:
          info.Rebase(stdout, stderr)
        else:
          info.Diff(stdout, stderr)
        status.Passed(info, duration)
  except Exception as e:
    status.Failed(info, str(e))


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('-a', '--arg',
                      help='additional args to pass to executable',
                      action='append')
  parser.add_argument('-o', '--out-dir', metavar='PATH',
                      help='output directory for files.')
  parser.add_argument('-e', '--sexpr-wasm-executable', metavar='PATH',
                      help='override executable.')
  parser.add_argument('--d8-executable', metavar='PATH',
                      help='override d8 executable.')
  parser.add_argument('--wasm-wast-executable', metavar='PATH',
                      help='override wasm-wast executable.')
  parser.add_argument('-v', '--verbose', help='print more diagnotic messages.',
                      action='store_true')
  parser.add_argument('-l', '--list', help='list all tests.',
                      action='store_true')
  parser.add_argument('-r', '--rebase',
                      help='rebase a test to its current output.',
                      action='store_true')
  parser.add_argument('-j', '--jobs', help='number of jobs to use to run tests',
                      type=int, default=multiprocessing.cpu_count())
  parser.add_argument('-t', '--timeout', type=float, default=DEFAULT_TIMEOUT,
                      help='per test timeout in seconds')
  parser.add_argument('--no-roundtrip',
                      help='don\'t run roundtrip.py on all tests',
                      action='store_false', default=True, dest='roundtrip')
  parser.add_argument('patterns', metavar='pattern', nargs='*',
                      help='test patterns.')
  options = parser.parse_args(args)

  if options.patterns:
    pattern_re = '|'.join(fnmatch.translate('*%s*' % p)
                          for p in options.patterns)
  else:
    pattern_re = '.*'

  test_names = findtests.FindTestFiles(SCRIPT_DIR, '.txt', pattern_re)
  if options.list:
    for test_name in test_names:
      print test_name
    return 0
  if not test_names:
    print 'no tests match that filter'
    return 1

  sexpr_wasm_exe = find_exe.GetSexprWasmExecutable(
      options.sexpr_wasm_executable)
  d8_exe = find_exe.GetD8Executable(options.d8_executable)
  wasm_wast_exe = find_exe.GetWasmWastExecutable(options.wasm_wast_executable)
  variables = {
    'sexpr-wasm': sexpr_wasm_exe,
    'd8': d8_exe,
    'wasm-wast': wasm_wast_exe
  }

  status = Status(options.verbose)
  infos = GetAllTestInfo(test_names, status)

  inq = multiprocessing.Queue()
  test_count = 0
  for info in infos:
    if info.skip:
      status.Skipped(info)
      continue
    inq.put(info)
    test_count += 1

    if options.roundtrip:
      if info.ShouldCreateRoundtrip():
        inq.put(info.CreateRoundtripInfo())
        test_count += 1

  outq = multiprocessing.Queue()
  num_proc = options.jobs
  status.Start(test_count)

  if options.out_dir:
    out_dir = options.out_dir
    out_dir_is_temp = False
    if not os.path.exists(out_dir):
      os.makedirs(out_dir)
  else:
    out_dir = tempfile.mkdtemp(prefix='sexpr-wasm-')
    out_dir_is_temp = True

  try:
    for i, p in enumerate(range(num_proc)):
      args = (i, options, variables, inq, outq, out_dir)
      proc = multiprocessing.Process(target=ProcessWorker, args=args)
      proc.start()

    finished_tests = 0
    while finished_tests < test_count:
      try:
        info, result = outq.get(True, 0.01)
      except Queue.Empty:
        status.UpdateTimer()
        continue

      finished_tests += 1
      HandleTestResult(status, info, result, options.rebase)
  except KeyboardInterrupt:
    while multiprocessing.active_children():
      time.sleep(0.1)
  finally:
    while multiprocessing.active_children():
      time.sleep(0.1)
    if out_dir_is_temp:
      shutil.rmtree(out_dir)

  status.Clear()

  ret = 0

  if status.failed:
    sys.stderr.write('**** FAILED %s\n' % ('*' * (80 - 14)))
    for info in status.failed_tests:
      sys.stderr.write('- %s\n' % info.name)
    ret = 1

  status.Print()
  return ret


if __name__ == '__main__':
  try:
    sys.exit(main(sys.argv[1:]))
  except Error as e:
    sys.stderr.write(str(e) + '\n')
    sys.exit(1)
