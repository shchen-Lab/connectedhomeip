#!/usr/bin/env python3
# Copyright (c) 2021 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import pathlib
import re
import sys
import logging
import coloredlogs
import shutil

import bflb_iot_tool
import bflb_iot_tool.__main__
import firmware_utils

coloredlogs.install(level='DEBUG')

# Additional options that can be use to configure an `Flasher`
# object (as dictionary keys) and/or passed as command line options.
BOUFFALO_OPTIONS = {
    # Configuration options define properties used in flashing operations.
    'configuration': {
        'chipname': {
            'help': "Bouffalolab chip name",
            'default': None,
            'argparse': {
                'metavar': 'CHIP_NAME',
            }
        },
        'pt': {
            'help': 'Partition table for board',
            'default': None,
            'argparse': {
                'metavar': 'PARTITION_TABLE_FILE',
                'type': pathlib.Path
            }
        },
        'dts': {
            'help': 'Device tree file',
            'default': None,
            'argparse': {
                'metavar': 'DEVICE_TREE_FILE',
                'type': pathlib.Path
            }
        },
        'xtal': {
            'help': 'XTAL for board',
            'default': None,
            'argparse': {
                'metavar': 'XTAL',
            }
        },
        'port': {
            'help': 'UART port to flash device',
            'default': None,
            'argparse': {
                'metavar': 'PORT',
            }
        },
        'baudrate': {
            'help': 'UART baudrate to flash device',
            'default': None,
            'argparse': {
                'metavar': 'BAUDRATE',
            },
        },
        'build': {
            'help': 'Deprecated, please use --build_ota or --build_ota_sign',
            'default': None,
            'argparse': {
                'action': 'store_true'
            }
        },
        'build_ota': {
            'help': 'Build ota image without singture hash included',
            'default': None,
            'argparse': {
                'action': 'store_true'
            }
        },
        'build_ota_sign': {
            'help': 'Build ota image with singture hash included. Must work with --pk and --sk',
            'default': None,
            'argparse': {
                'action': 'store_true'
            }
        },
        'ota': {
            'help': 'output directory of ota image build',
            'default': None,
            'argparse': {
                'metavar': 'DIR',
                'type': pathlib.Path
            }
        },
        'pk': {
            'help': 'public key to sign firmware to flash or sign ota image.',
            'default': None,
            'argparse': {
                'metavar': 'path',
                'type': pathlib.Path
            }
        },
        'sk': {
            'help': 'private key to sign firmware to flash or sign ota image.',
            'default': None,
            'argparse': {
                'metavar': 'path',
                'type': pathlib.Path
            }
        },
        'boot2': {
            'help': 'boot2 image.',
            'default': None,
            'argparse': {
                'metavar': 'path',
            }
        }
    },
}


class Flasher(firmware_utils.Flasher):

    isErase = False

    def __init__(self, **options):
        super().__init__(platform=None, module=__name__, **options)
        self.define_options(BOUFFALO_OPTIONS)

    def get_boot_image(self, config_path, boot2_image):

        boot_image_guess = None

        for root, dirs, files in os.walk(config_path, topdown=False):
            for name in files:
                logging.info("get_boot_image {} {}".format(root, boot2_image))
                if boot2_image:
                    return os.path.join(root, boot2_image)
                else:
                    if name == "boot2_isp_release.bin":
                        return os.path.join(root, name)
                    elif not boot_image_guess and name.find("release") >= 0:
                        boot_image_guess = os.path.join(root, name)

        return boot_image_guess

    def get_dts_file(self, config_path, xtal_value, chip_name):

        for root, dirs, files in os.walk(config_path, topdown=False):
            for name in files:
                if chip_name == 'bl616':
                    if name.find("bl_factory_params_IoTKitA_auto.dts") >= 0:
                        return os.path.join(config_path, name)
                else:
                    if name.find(xtal_value) >= 0:
                        return os.path.join(config_path, name)
        return None

    def verify(self):
        """Not supported"""
        self.log(0, "Verification is done after image flashed.")

    def reset(self):
        """Not supported"""
        self.log(0, "Reset is triggered automatically after image flashed.")

    def actions(self):
        """Perform actions on the device according to self.option."""
        self.log(3, 'Options:', self.option)

        tool_path = os.path.dirname(bflb_iot_tool.__file__)

        options_keys = BOUFFALO_OPTIONS["configuration"].keys()
        arguments = [__file__]
        work_dir = None

        if self.option.reset:
            self.reset()
        if self.option.verify_application:
            self.verify()

        chip_name = None
        chip_config_path = None
        boot2_image = None
        dts_path = None
        xtal_value = None

        is_for_ota_image_building = None
        is_for_programming = False
        has_private_key = False
        has_public_key = False
        ota_output_folder = None

        boot2_image = None

        command_args = {}
        for (key, value) in dict(vars(self.option)).items():

            if self.option.build and value:
                if "port" in command_args.keys():
                    continue
            else:
                if "ota" in command_args.keys():
                    continue

            if key == "application":
                key = "firmware"
                work_dir = os.path.dirname(os.path.join(os.getcwd(), str(value)))
            elif key == "boot2":
                boot2_image = value
                continue
            elif key in options_keys:
                pass
            else:
                continue

            if value:
                if value is True:
                    if "build_ota" == key:
                        is_for_ota_image_building = "ota"
                        arg = ("--{}".format("build")).strip()
                    elif "build_ota_sign" == key:
                        is_for_ota_image_building = "ota_sign"
                        arg = ("--{}".format("build")).strip()
                    else:
                        arg = ("--{}".format(key)).strip()
                elif isinstance(value, pathlib.Path):
                    arg = ("--{}={}".format(key, os.path.join(os.getcwd(), str(value)))).strip()
                else:
                    arg = ("--{}={}".format(key, value)).strip()

            if key == "chipname":
                chip_name = value
            elif key == "xtal":
                xtal_value = value
            elif key == "dts":
                dts_path = value
            elif "port" == key:
                if value:
                    is_for_programming = True
            elif "build" == key:

                if value:
                    logging.error("*" * 80)
                    logging.error("\t\t--build is deprecated.")
                    logging.error("")

                    logging.error("Real product should has hardware signture enabled to protect firmware to be hacked.")
                    logging.error("To be avoid: An ota image with invalid signture is upgraded to device in real product.")
                    logging.error("             Public key hash should be verified during ota upgrade process.")
                    logging.error("             Use --build_ota_sign to build ota image with public key added.")

                    logging.error("Development is not necessary to have hardware signture enabled. ")
                    logging.error("             Use --build_ota to build ota image without public key added.")

                    logging.error("*" * 80)

                    raise Exception("Wrong options.")
            elif "pk" == key:
                if value:
                    has_public_key = True
            elif "sk" == key:
                if value:
                    has_private_key = True
            elif "ota" == key and value:
                ota_output_folder = os.path.join(os.getcwd(), value)

            arguments.append(arg)

        if is_for_ota_image_building and is_for_programming:
            logging.error("ota imge build can't work with image programming")
            raise Exception("Wrong operation.")

        if not ((has_private_key and has_public_key) or (not has_public_key and not has_private_key)):
            logging.error("Key pair expects a pair of public key and private.")
            raise Exception("Wrong key pair.")

        if is_for_ota_image_building == "ota_sign" and (not has_private_key or not has_public_key):
            logging.error("Expecting key pair to sign OTA image.")
            raise Exception("Wrong key pair.")

        if not dts_path and xtal_value:
            chip_config_path = os.path.join(tool_path, "chips", chip_name, "device_tree")
            dts_path = self.get_dts_file(chip_config_path, xtal_value, chip_name)
            arguments.append("--dts")
            arguments.append(dts_path)

        if boot2_image:
            chip_config_path = os.path.join(tool_path, "chips", chip_name, "builtin_imgs")
            boot2_image = self.get_boot_image(chip_config_path, boot2_image)
            arguments.append("--boot2")
            arguments.append(boot2_image)
        else:
            if self.option.erase:
                arguments.append("--erase")

                if chip_name == "bl602":
                    chip_config_path = os.path.join(tool_path, "chips", chip_name, "builtin_imgs")
                    boot2_image = self.get_boot_image(chip_config_path, boot2_image)
                    arguments.append("--boot2")
                    arguments.append(boot2_image)

        os.chdir(work_dir)
        arguments[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', arguments[0])
        sys.argv = arguments

        if ota_output_folder:
            if os.path.exists(ota_output_folder):
                shutil.rmtree(ota_output_folder)
            os.mkdir(ota_output_folder)

        logging.info("Arguments {}".format(arguments))
        bflb_iot_tool.__main__.run_main()

        if ota_output_folder:
            ota_images = os.listdir(ota_output_folder)
            for img in ota_images:
                if img not in ['FW_OTA.bin.xz.ota', 'FW_OTA.bin.xz.hash']:
                    os.remove(os.path.join(ota_output_folder, img))

        return self

if __name__ == '__main__':

    sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])

    sys.exit(Flasher().flash_command(sys.argv))
