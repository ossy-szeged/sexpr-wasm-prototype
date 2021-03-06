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
import os
import signal
import subprocess
import sys
import tempfile

import find_exe
from utils import Error

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXPOSE_WASM = '--expose-wasm'
WASM_JS = os.path.join(SCRIPT_DIR, 'wasm.js')
SPEC_JS = os.path.join(SCRIPT_DIR, 'spec.js')

# Get signal names from numbers in Python
# http://stackoverflow.com/a/2549950
SIGNAMES = dict((k, v) for v, k in reversed(sorted(signal.__dict__.items()))
                       if v.startswith('SIG') and not v.startswith('SIG_'))

def CleanD8Stdout(stdout):
  def FixLine(line):
    idx = line.find('WasmModule::Instantiate()')
    if idx != -1:
      return line[idx:]
    return line
  lines = [FixLine(line) for line in stdout.splitlines(1)]
  return ''.join(lines)


def CleanD8Stderr(stderr):
  idx = stderr.find('==== C stack trace')
  if idx != -1:
    stderr = stderr[:idx]
  return stderr.strip()


def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('-e', '--executable', metavar='PATH',
                      help='override sexpr-wasm executable.')
  parser.add_argument('--d8-executable', metavar='PATH',
                      help='override d8 executable.')
  parser.add_argument('-v', '--verbose', help='print more diagnotic messages.',
                      action='store_true')
  parser.add_argument('--use-libc-allocator', action='store_true')
  parser.add_argument('--spec', help='run spec tests.', action='store_true')
  parser.add_argument('file', help='test file.')
  options = parser.parse_args(args)

  sexpr_wasm_exe = find_exe.GetSexprWasmExecutable(options.executable)
  d8_exe = find_exe.GetD8Executable(options.d8_executable)

  generated = None
  try:
    # Use delete=False because Windows can't open a NamedTemporaryFile until it
    # is cloesd, but it will be deleted by default if it is closed.
    generated = tempfile.NamedTemporaryFile(prefix='sexpr-wasm-', delete=False)
    generated.close()
    wasm_file = generated.name
    # First compile the file
    cmd = [sexpr_wasm_exe, '-o', wasm_file]
    if options.verbose:
      cmd.append('-v')
    if options.spec:
      cmd.extend(['--spec', '--spec-verbose'])
    if options.use_libc_allocator:
      cmd.extend(['--use-libc-allocator'])
    cmd.append(options.file)
    try:
      process = subprocess.Popen(cmd, stderr=subprocess.PIPE)
      _, stderr = process.communicate()
      if process.returncode != 0:
        raise Error(stderr)
    except OSError as e:
      raise Error(str(e))

    # Now run the generated file
    if options.spec:
      # The generated file is JavaScript, so run it directly.
      cmd = [d8_exe, EXPOSE_WASM, SPEC_JS, wasm_file]
    else:
      cmd = [d8_exe, EXPOSE_WASM, WASM_JS, '--', wasm_file]
    try:
      process = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE,
                                 universal_newlines=True)
      stdout, stderr = process.communicate()
      sys.stdout.write(CleanD8Stdout(stdout))
      msg = CleanD8Stderr(stderr)
      if process.returncode < 0:
        # Terminated by signal
        signame = SIGNAMES.get(-process.returncode, '<unknown>')
        if msg:
          msg += '\n\n'
        raise Error(msg + 'd8 raised signal ' + signame)
      elif process.returncode > 0:
        raise Error(msg)
    except OSError as e:
      raise Error(str(e))

  finally:
    if generated:
      os.remove(generated.name)

  return 0


if __name__ == '__main__':
  try:
    sys.exit(main(sys.argv[1:]))
  except Error as e:
    sys.stderr.write(str(e) + '\n')
    sys.exit(1)
