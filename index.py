#
#	MetaCall Python C io_uring Example by Parra Studios
#	An example of embedding io_uring (from C) into Python.
#
#	Copyright (C) 2016 - 2021 Vicente Eduardo Ferrer Garcia <vic798@gmail.com>
#
#	Licensed under the Apache License, Version 2.0 (the "License");
#	you may not use this file except in compliance with the License.
#	You may obtain a copy of the License at
#
#		http://www.apache.org/licenses/LICENSE-2.0
#
#	Unless required by applicable law or agreed to in writing, software
#	distributed under the License is distributed on an "AS IS" BASIS,
#	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#	See the License for the specific language governing permissions and
#	limitations under the License.
#
#

from metacall import metacall_load_from_file, metacall

metacall_load_from_file('c', ['uring.c', 'script.ld'])

def fibonacci(n: int) -> int:
    try:
        a, b = 0, 1
        for i in range(0, n):
            a, b = b, a + b
        return a
    except:
        return 0

metacall('server_listen', 8000, fibonacci)
