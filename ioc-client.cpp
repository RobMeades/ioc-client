/* Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>
#include <stdint.h>
#include <compile_time.h>
#include <utils.h>
#include <log.h>

int main(int argc, char *argv[])
{
    LOG(EVENT_SYSTEM_START, getUSeconds() / 1000000);
    LOG(EVENT_BUILD_TIME_UNIX_FORMAT, __COMPILE_TIME_UNIX__);

    return 0;
}