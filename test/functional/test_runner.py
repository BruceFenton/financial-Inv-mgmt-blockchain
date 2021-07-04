#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Run regression test suite.

This module calls down into individual test cases via subprocess. It will
forward all unrecognized arguments onto the individual test scripts.

Functional tests are disabled on Windows by default. Use --force to run them anyway.

For a description of arguments recognized by test scripts, see
`test/functional/test_framework/test_framework.py:RavenTestFramework.main`.


"""

from collections import deque
import argparse
import configparser
import datetime
import os
import time
import shutil
import signal
import sys
import subprocess
import tempfile
import re
import logging

# Formatting. Default colors to empty strings.
BOLD, GREEN, RED, GREY = ("", ""), ("", ""), ("", ""), ("", "")

try:
    # Make sure python thinks it can write unicode to its stdout
    "\u2713".encode("utf_8").decode(sys.stdout.encoding)
    TICK = "✓ "
    CROSS = "✖ "
    CIRCLE = "○ "
    DASH = "- "
except UnicodeDecodeError:
    TICK = "P "
    CROSS = "x "
    CIRCLE = "o "
    DASH = "- "

if os.name == 'posix':
    # primitive formatting on supported
    # terminal via ANSI escape sequences:
    BOLD = ('\033[0m', '\033[1m')
    GREEN = ('\033[0m', '\033[0;32m')
    RED = ('\033[0m', '\033[0;31m')
    GREY = ('\033[0m', '\033[1;30m')

TEST_EXIT_PASSED = 0
TEST_EXIT_SKIPPED = 77

EXTENDED_SCRIPTS = [
    # These tests are not run by the build process.
    # Longest test should go first, to favor running tests in parallel
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 20m vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'feature_fee_estimation.py',
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 5m vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'feature_dbcrash.py',
]

BASE_SCRIPTS= [
    # Scripts that are run by the build process.
    # Longest test should go first, to favor running tests in parallel
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 2m vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'wallet_backup.py',
    'wallet_hd.py',
    'p2p_timeouts.py',
    'mining_getblocktemplate_longpoll.py',
    'feature_maxuploadtarget.py',
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 45s vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'rpc_fundrawtransaction.py',
    'wallet_create_tx.py',
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 30s vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'feature_rewards.py',
    'wallet_basic.py',
    'mempool_limit.py',
    'feature_assets.py',
    'feature_assets_p2sh.py',
    'feature_messaging.py',
    'feature_assets_reorg.py',
    'feature_assets_mempool.py',
    'feature_restricted_assets.py',
    'feature_raw_restricted_assets.py',
    'wallet_bip44.py',    
    'wallet_bip44_multilanguage.py',
    'mining_prioritisetransaction.py',
    'feature_maxreorgdepth.py 4 --height=60 --tip_age=0 --should_reorg=0',      # Don't Reorg
    'feature_maxreorgdepth.py 3 --height=60 --tip_age=0 --should_reorg=1',      # Reorg (low peer count)
    'feature_maxreorgdepth.py 4 --height=60 --tip_age=43400 --should_reorg=1',  # Reorg (not caught up)
    'feature_maxreorgdepth.py 4 --height=59 --tip_age=0 --should_reorg=1',      # Reorg (<60)
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 15s vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'rpc_rawtransaction.py',
    'rpc_addressindex.py',
    'wallet_dump.py',
    'mempool_persist.py',
    'rpc_timestampindex.py',
    'wallet_listreceivedby.py',
    'wallet_reorgsrestore.py',
    'interface_rest.py',
    'wallet_keypool_topup.py',
    'wallet_import_rescan.py',
    'wallet_abandonconflict.py',
    'wallet_groups.py',
    'rpc_blockchain.py',
    'p2p_feefilter.py',
    'p2p_leak.py',
    'feature_versionbits_warning.py',
    'rpc_spentindex.py',
    'feature_rawassettransactions.py',
    'wallet_importmulti.py',
    'wallet_labels.py',
    'wallet_import_with_label.py',
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 5s vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'wallet_listtransactions.py',
    'feature_minchainwork.py',
    'wallet_encryption.py',
    'feature_listmyassets.py',
    'mempool_reorg.py',
    'rpc_txoutproof.py',
    'feature_reindex.py',
    'rpc_decodescript.py',
    'wallet_keypool.py',
    'rpc_setban.py',
    'wallet_listsinceblock.py',
    'wallet_zapwallettxes.py',
    'wallet_multiwallet.py',
    'interface_zmq.py',
    'rpc_invalidateblock.py',
    # vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv Tests less than 3s vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
    'rpc_getchaintips.py',
    'wallet_txn_clone.py',
    'wallet_txn_doublespend.py --mineblock',
    'feature_uacomment.py',
    'rpc_users.py',
    'feature_proxy.py',
    'rpc_txindex.py',
    'p2p_disconnect_ban.py',
    'wallet_importprunedfunds.py',
    'rpc_bind.py',
    'feature_unique_assets.py',
    'rpc_preciousblock.py',
    'feature_notifications.py',
    'rpc_net.py',
    'rpc_misc.py',
    'interface_raven_cli.py',
    'mempool_resurrect.py',
    'rpc_signrawtransaction.py',
    'wallet_resendtransactions.py',
    'wallet_txn_clone.py --mineblock',
    'interface_rpc.py',
    'rpc_signmessage.py',
    'rpc_deprecated.py',
    'wallet_coinbase_category.py',
    'wallet_txn_doublespend.py',
    'feature_shutdown.py',
    'wallet_disable.py',
    'interface_http.py',
    'mempool_spend_coinbase.py',
    'feature_bip68_sequence.py',
    'p2p_mempool.py',
    'rpc_named_arguments.py',
    'rpc_uptime.py',
    'rpc_assettransfer.py',
    'feature_loadblock.py',
    'p2p_leak_tx.py'
    # Don't append tests at the end to avoid merge conflicts
    # Put them in a random line within the section that fits their approximate run-time
]

SKIPPED_TESTS = [
    # List of tests that are not going to be run (usually means test is broken)
    'example_test.py',
    'feature_assumevalid.py',
    'feature_cltv.py',              #TODO - fix mininode rehash methods to use X16R
    'feature_dersig.py',            #TODO - fix mininode rehash methods to use X16R
    'feature_nulldummy.py',         #TODO - fix mininode rehash methods to use X16R
    'feature_pruning.py',
    'feature_rbf.py',
    'feature_segwit.py',            #TODO - fix mininode rehash methods to use X16R
    'mempool_packages.py',
    'mining_basic.py',              #TODO - fix mininode rehash methods to use X16R
    'p2p_compactblocks.py',         #TODO - refactor to assume segwit is always active
    'p2p_fingerprint.py',           #TODO - fix mininode rehash methods to use X16R
    'p2p_segwit.py',                #TODO - refactor to assume segwit is always active
    'p2p_sendheaders.py',           #TODO - fix mininode rehash methods to use X16R
    'p2p_unrequested_blocks.py',
    'wallet_bumpfee.py',            #TODO - Now fails because we removed RBF
]

# Place EXTENDED_SCRIPTS first since it has the 3 longest running tests
ALL_SCRIPTS = EXTENDED_SCRIPTS + BASE_SCRIPTS

NON_SCRIPTS = [
    # These are python files that live in the functional tests directory, but are not test scripts.
    "combine_logs.py",
    "create_cache.py",
    "test_runner.py",
]


def main():
    # Parse arguments and pass through unrecognised args
    parser = argparse.ArgumentParser(add_help=False, usage='%(prog)s [test_runner.py options] [script options] [scripts]', description=__doc__,
                                     epilog='Help text and arguments for individual test script:', formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('--ansi', action='store_true', default=sys.stdout.isatty(), help='Use ANSI colors and dots in output (enabled by default when standard output is a TTY)')
    parser.add_argument('--combinedlogslen', type=int, default=0, metavar='n', help='On failure, print a log (of length n lines) to the console, combined from the test framework and all test nodes.')
    parser.add_argument('--coverage', action='store_true', help='Generate a basic coverage report for the RPC interface.')
    parser.add_argument('--exclude', metavar='', help='Specify a comma-separated-list of scripts to exclude.')
    parser.add_argument('--extended', action='store_true', help='Run the extended test suite in addition to the basic tests.')
    parser.add_argument('--failfast', action='store_true', help='Stop execution after the first test failure.')
    parser.add_argument('--filter', metavar='', help='Filter scripts to run by regular expression.')
    parser.add_argument('--force', action='store_true', help='Run tests even on platforms where they are disabled by default (e.g. windows).')
    parser.add_argument('--help', action='store_true', help='Print help text and exit.')
    parser.add_argument('--jobs', type=int, metavar='', default=get_cpu_count(), help='How many test scripts to run in parallel. Default=.' + str(get_cpu_count()))
    parser.add_argument('--keepcache', action='store_true', help='The default behavior is to flush the cache directory on startup. --keepcache retains the cache from the previous test-run.')
    parser.add_argument('--list', action='store_true', help='Print list of tests and exit.')
    parser.add_argument('--loop', type=int, metavar='n', default=1, help='Run(loop) the tests n number of times.')
    parser.add_argument('--onlyextended', action='store_true', help='Run only the extended test suite.')
    parser.add_argument('--quiet',  action='store_true', help='Only print results summary and failure logs.')
    parser.add_argument('--tmpdirprefix', metavar='', default=tempfile.gettempdir(), help='Root directory for data.')


    # Setup colours for ANSI terminals
    args, unknown_args = parser.parse_known_args()
    if not args.ansi:
        global BOLD, GREEN, RED, GREY
        BOLD = ("", "")
        GREEN = ("", "")
        RED = ("", "")
        GREY = ("", "")

    # args to be passed on always start with two dashes; tests are the remaining unknown args
    tests = [arg for arg in unknown_args if arg[:2] != "--"]
    pass_on_args = [arg for arg in unknown_args if arg[:2] == "--"]

    # Read config generated by configure.
    config = configparser.ConfigParser()
    configfile = os.path.abspath(os.path.dirname(__file__)) + "/../config.ini"
    config.read_file(open(configfile, encoding="utf8"))
    pass_on_args.append("--configfile=%s" % configfile)

    # Set up logging
    logging_level = logging.INFO if args.quiet else logging.DEBUG
    logging.basicConfig(format='%(message)s', level=logging_level)

    # Create base test directory
    tmpdir = "%s/raven_test_runner_%s" % (args.tmpdirprefix, datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(tmpdir)
    logging.debug("Temporary test directory at %s" % tmpdir)

    # Don't run tests on Windows by default
    if config["environment"]["EXEEXT"] == ".exe" and not args.force:
        # https://github.com/bitcoin/bitcoin/commit/d52802551752140cf41f0d9a225a43e84404d3e9
        # https://github.com/bitcoin/bitcoin/pull/5677#issuecomment-136646964
        print("Tests currently disabled on Windows by default. Use --force option to enable")
        sys.exit(0)

    # Check that the build was configured with wallet, utils, and ravend
    enable_wallet = config["components"].getboolean("ENABLE_WALLET")
    enable_cli = config["components"].getboolean("ENABLE_UTILS")
    enable_ravend = config["components"].getboolean("ENABLE_RAVEND")
    if not (enable_wallet and enable_cli and enable_ravend):
        print("No functional tests to run. Wallet, utils, and ravend must all be enabled")
        print("Rerun `configure` with --enable-wallet, --with-cli and --with-daemon and rerun make")
        sys.exit(0)

    # Loop the running of tests
    for i in range(0, args.loop):
        print("Test Loop ", i+1, "of ", args.loop)
        last_loop = False
        if i+1 == args.loop:
            last_loop = True

        # Build list of tests
        test_list = []
        if tests:
            # Individual tests have been specified. Run specified tests that exist
            # in the ALL_SCRIPTS list. Accept names with or without a .py extension.
            # Specified tests can contain wildcards, but in that case the supplied
            # paths should be coherent, e.g. the same path as that provided to call
            # test_runner.py. Examples:
            #   `test/functional/test_runner.py test/functional/wallet*`
            #   `test/functional/test_runner.py ./test/functional/wallet*`
            #   `test_runner.py wallet*`
            #   but not:
            #   `test/functional/test_runner.py wallet*`
            # Multiple wildcards can be passed:
            #   `test_runner.py tool* mempool*`
            for test in tests:
                script = test.split("/")[-1]
                script = script + ".py" if ".py" not in script else script
                if script in ALL_SCRIPTS:
                    test_list.append(script)
                else:
                    print("{}WARNING!{} Test '{}' not found in full test list.".format(BOLD[1], BOLD[0], test))
        elif args.extended:
            # Include extended tests
            test_list += ALL_SCRIPTS
        else:
            # Run base tests only
            test_list += BASE_SCRIPTS

        # Remove the test cases that the user has explicitly asked to exclude.
        if args.exclude:
            exclude_tests = [test.split('.py')[0] for test in args.exclude.split(',')]
            for exclude_test in exclude_tests:
                # Remove <test_name>.py and <test_name>.py --arg from the test list
                exclude_list = [test for test in test_list if test.split('.py')[0] == exclude_test]
                for exclude_item in exclude_list:
                    test_list.remove(exclude_item)
                if not exclude_list:
                    print("{}WARNING!{} Test '{}' not found in current test list.".format(BOLD[1], BOLD[0], exclude_test))

        if args.filter:
            test_list = list(filter(re.compile(args.filter).search, test_list))

        if not test_list:
            print("No valid test scripts specified. Check that your test is in one "
                  "of the test lists in test_runner.py, or run test_runner.py with no arguments to run all tests")
            sys.exit(0)

        if args.help:
            # Print help for test_runner.py, then print help of the first script (with args removed) and exit.
            parser.print_help()
            subprocess.check_call([(config["environment"]["SRCDIR"] + '/test/functional/' + test_list[0].split()[0])] + ['-h'])
            sys.exit(0)

        if args.list:
            print(ALL_SCRIPTS)
            sys.exit(0)

        check_script_list(config["environment"]["SRCDIR"])
        check_script_prefixes()

        if not args.keepcache:
            shutil.rmtree("%s/test/cache" % config["environment"]["BUILDDIR"], ignore_errors=True)

        run_tests(
            test_list=test_list,
            src_dir=config["environment"]["SRCDIR"],
            build_dir=config["environment"]["BUILDDIR"],
            exeext=config["environment"]["EXEEXT"],
            tmpdir=tmpdir,
            use_term_control=args.ansi,
            jobs=args.jobs,
            enable_coverage=args.coverage,
            args=pass_on_args,
            combined_logs_len=args.combinedlogslen,
            failfast=args.failfast,
            last_loop=last_loop
        )


def run_tests(test_list, src_dir, build_dir, exeext, tmpdir, use_term_control, jobs=1, enable_coverage=False, args=None, combined_logs_len=0, failfast=False, last_loop=False):
    # Warn if ravend is already running (unix only)
    if args is None:
        args = []
    try:
        if subprocess.check_output(["pidof", "ravend"]) is not None:
            print("%sWARNING!%s There is already a ravend process running on this system. Tests may fail unexpectedly due to resource contention!" % (BOLD[1], BOLD[0]))
    except (OSError, subprocess.SubprocessError):
        pass

    # Warn if there is a cache directory
    cache_dir = "%s/test/cache" % build_dir
    if os.path.isdir(cache_dir):
        print("%sWARNING!%s There is a cache directory here: %s. If tests fail unexpectedly, try deleting the cache directory." % (BOLD[1], BOLD[0], cache_dir))

    #Set env vars
    if "RAVEND" not in os.environ:
        os.environ["RAVEND"] = build_dir + '/src/ravend' + exeext
        os.environ["RAVENCLI"] = build_dir + '/src/raven-cli' + exeext

    tests_dir = src_dir + '/test/functional/'

    # limit number of jobs to 13
    if jobs > 13:
        jobs = 13
        print("Jobs limited to 13 threads max.")
    print("Using: ", jobs, " threads")

    flags = ["--srcdir={}/src".format(build_dir)] + args
    flags.append("--cachedir=%s" % cache_dir)

    if enable_coverage:
        coverage = RPCCoverage()
        flags.append(coverage.flag)
        logging.debug("Initializing coverage directory at %s" % coverage.dir)
    else:
        coverage = None

    if len(test_list) > 1 and jobs > 1:
        # Populate cache
        try:
            subprocess.check_output([tests_dir + 'create_cache.py'] + flags + ["--tmpdir=%s/cache" % tmpdir])
        except subprocess.CalledProcessError as e:
            print("\n----<test_runner>----\n")
            print("Error in create_cache.py:\n")
            for line in e.output.decode().split('\n'):
                print(line)
            print('\n')
            print(e.returncode)
            print('\n')
            print("\n----</test_runner>---\n")
            raise

    #Run Tests
    job_queue = TestHandler(
        num_tests_parallel=jobs,
        tests_dir=tests_dir,
        tmpdir=tmpdir,
        use_term_control=use_term_control,
        test_list=test_list,
        flags=flags
    )

    start_time = time.time()
    test_results = []

    max_len_name = len(max(test_list, key=len))
    test_count = len(test_list)

    for _ in range(test_count):
        test_result, testdir, stdout, stderr = job_queue.get_next()
        test_results.append(test_result)
        done_str = "{}/{} - {}{}{}".format(_ + 1, test_count, BOLD[1], test_result.name, BOLD[0])
        if test_result.status == "Passed":
            logging.debug("%s passed, Duration: %s s" % (done_str, test_result.time))
        elif test_result.status == "Skipped":
            logging.debug("%s skipped" % done_str)
        else:
            print("%s failed, Duration: %s s\n" % (done_str, test_result.time))
            print(BOLD[1] + 'stdout:\n' + BOLD[0] + stdout + '\n')
            print(BOLD[1] + 'stderr:\n' + BOLD[0] + stderr + '\n')

            if combined_logs_len and os.path.isdir(testdir):
                # Print the final `combinedlogslen` lines of the combined logs
                print('{}Combine the logs and print the last {} lines ...{}'.format(BOLD[1], combined_logs_len, BOLD[0]))
                print('\n============')
                print('{}Combined log for {}:{}'.format(BOLD[1], testdir, BOLD[0]))
                print('============\n')
                combined_logs_args = [sys.executable, os.path.join(tests_dir, 'combine_logs.py'), testdir]
                if BOLD[0]:
                    combined_logs_args += ['--color']
                combined_logs, _ = subprocess.Popen(combined_logs_args, universal_newlines=True, stdout=subprocess.PIPE).communicate()
                print("\n".join(deque(combined_logs.splitlines(), combined_logs_len)))

            if failfast:
                logging.debug("Early exit after test failure...")
                break

    print_results(test_results, max_len_name, (int(time.time() - start_time)))

    if coverage:
        coverage_passed = coverage.report_rpc_coverage()

        logging.debug("Cleaning up coverage data")
        coverage.cleanup()
    else:
        coverage_passed = True

    # Clear up the temp directory if all subdirectories are gone
    if not os.listdir(tmpdir):
        os.rmdir(tmpdir)

    all_passed = all(map(lambda test_res: test_res.was_successful, test_results)) and coverage_passed

    # This will be a no-op unless failfast is True in which case there may be dangling
    # processes which need to be killed.
    job_queue.kill_and_join()

    if last_loop:
        sys.exit(not all_passed)


def print_results(test_results, max_len_name, runtime):
    results = "\n" + BOLD[1] + "%s | %s | %s\n\n" % ("TEST".ljust(max_len_name), "STATUS   ", "DURATION") + BOLD[0]

    test_results.sort(key=TestResult.sort_key)
    all_passed = True
    time_sum = 0

    for test_result in test_results:
        all_passed = all_passed and test_result.was_successful
        time_sum += test_result.time
        test_result.padding = max_len_name
        results += str(test_result)

    status = TICK + "Passed" if all_passed else CROSS + "Failed"
    if not all_passed:
        results += RED[1]
    results += BOLD[1] + "\n%s | %s | %s s (accumulated) \n" % ("ALL".ljust(max_len_name), status.ljust(9), time_sum) + BOLD[0]
    if not all_passed:
        results += RED[0]
    results += "Runtime: %s s\n" % runtime
    print(results)


# noinspection PyTypeChecker
class TestHandler:
    """
    Trigger the test scripts passed in via the list.
    """

    def __init__(self, num_tests_parallel, tests_dir, tmpdir, use_term_control, test_list=None, flags=None):
        assert(num_tests_parallel >= 1)
        self.num_jobs = num_tests_parallel
        self.tests_dir = tests_dir
        self.tmpdir = tmpdir
        self.use_term_control = use_term_control
        self.test_list = test_list
        self.flags = flags
        self.num_running = 0
        self.jobs = []


    def get_next(self):
        while self.num_running < self.num_jobs and self.test_list:
            # Add tests
            self.num_running += 1
            test = self.test_list.pop(0)
            port_seed = len(self.test_list)
            port_seed_arg = ["--portseed={}".format(port_seed)]
            log_stdout = tempfile.SpooledTemporaryFile(max_size=2**16)
            log_stderr = tempfile.SpooledTemporaryFile(max_size=2**16)
            test_argv = test.split()
            test_dir = "{}/{}_{}".format(self.tmpdir, re.sub(".py$", "", test_argv[0]), port_seed)
            tmpdir_arg = ["--tmpdir={}".format(test_dir)]
            self.jobs.append((test,
                              time.time(),
                              subprocess.Popen([self.tests_dir + test_argv[0]] + test_argv[1:] + self.flags + port_seed_arg + tmpdir_arg, universal_newlines=True, stdout=log_stdout, stderr=log_stderr),
                              test_dir,
                              log_stdout,
                              log_stderr))
        if not self.jobs:
            raise IndexError('pop from empty list')

        dot_count = 0
        while True:
            # Return first proc that finishes
            time.sleep(.5)
            for job in self.jobs:
                (name, start_time, proc, test_dir, log_out, log_err) = job
                if int(time.time() - start_time) > 20 * 60:
                    # Timeout individual tests after 20 minutes (to stop tests hanging and not
                    # providing useful output.
                    proc.send_signal(signal.SIGINT)
                if proc.poll() is not None:
                    log_out.seek(0), log_err.seek(0)
                    [stdout, stderr] = [log_file.read().decode('utf-8') for log_file in (log_out, log_err)]
                    log_out.close(), log_err.close()
                    if proc.returncode == TEST_EXIT_PASSED and stderr == "":
                        status = "Passed"
                    elif proc.returncode == TEST_EXIT_SKIPPED:
                        status = "Skipped"
                    else:
                        status = "Failed"
                    self.num_running -= 1
                    self.jobs.remove(job)
                    if self.use_term_control:
                        clear_line = '\r' + (' ' * dot_count) + '\r'
                        print(clear_line, end='', flush=True)

                    return TestResult(name, status, int(time.time() - start_time)), test_dir, stdout, stderr
            if self.use_term_control:
                print('.', end='', flush=True)
            dot_count += 1

    def kill_and_join(self):
        """Send SIGKILL to all jobs and block until all have ended."""
        process = [i[2] for i in self.jobs]

        for p in process:
            p.kill()

        for p in process:
            p.wait()

class TestResult:
    def __init__(self, name, status, result_time):
        self.name = name
        self.status = status
        self.time = result_time
        self.padding = 0

    def sort_key(self):
        if self.status == "Passed":
            return 0, self.name.lower()
        elif self.status == "Failed":
            return 2, self.name.lower()
        elif self.status == "Skipped":
            return 1, self.name.lower()

    def __repr__(self):
        if self.status == "Passed":
            color = GREEN
            glyph = TICK
        elif self.status == "Failed":
            color = RED
            glyph = CROSS
        elif self.status == "Skipped":
            color = GREY
            glyph = CIRCLE
        else:
            color = BOLD
            glyph = DASH

        return color[1] + "%s | %s%s | %s s\n" % (self.name.ljust(self.padding), glyph, self.status.ljust(7), self.time) + color[0]

    @property
    def was_successful(self):
        return self.status != "Failed"


def check_script_prefixes():
    """Check that no more than `expected_violation_count` of the
       test scripts don't start with one of the allowed name prefixes."""
    expected_violation_count = 0
    # leeway is provided as a transition measure, so that pull-requests
    # that introduce new tests that don't conform with the naming
    # convention don't immediately cause the tests to fail.
    leeway = 1
    good_prefixes_re = re.compile("(example|feature|interface|mempool|mining|p2p|rpc|wallet)_")
    bad_script_names = [script for script in ALL_SCRIPTS if good_prefixes_re.match(script) is None]
    if len(bad_script_names) < expected_violation_count:
        print("{}HURRAY!{} Number of functional tests violating naming convention reduced!".format(BOLD[1], BOLD[0]))
        print("Consider reducing expected_violation_count from %d to %d" % (expected_violation_count, len(bad_script_names)))
    elif len(bad_script_names) > expected_violation_count:
        print("WARNING: %d tests not meeting naming conventions.  Please rename with allowed prefix. (expected %d):" % (len(bad_script_names), expected_violation_count))
        print("  %s" % ("\n  ".join(sorted(bad_script_names))))
        assert len(bad_script_names) <= expected_violation_count + leeway, "Too many tests not following naming convention! (%d found, expected: <= %d)" % (len(bad_script_names), expected_violation_count)


def check_script_list(src_dir):
    """Check scripts directory.

    Check that there are no scripts in the functional tests directory which are
    not being run by pull-tester.py."""
    script_dir = src_dir + '/test/functional/'
    python_files = set([t for t in os.listdir(script_dir) if t[-3:] == ".py"])
    missed_tests = list(python_files - set(map(lambda x: x.split()[0], ALL_SCRIPTS + NON_SCRIPTS + SKIPPED_TESTS)))
    if len(missed_tests) != 0:
        print("%sWARNING!%s The following scripts are not being run:\n%s \nCheck the test lists in test_runner.py." % (BOLD[1], BOLD[0], "\n".join(missed_tests)))


def get_cpu_count():
    try:
        import multiprocessing
        return multiprocessing.cpu_count()
    except ImportError:
        return 4


class RPCCoverage:
    """
    Coverage reporting utilities for test_runner.

    Coverage calculation works by having each test script subprocess write
    coverage files into a particular directory. These files contain the RPC
    commands invoked during testing, as well as a complete listing of RPC
    commands per `raven-cli help` (`rpc_interface.txt`).

    After all tests complete, the commands run are combined and diff'd against
    the complete list to calculate uncovered RPC commands.

    See also: test/functional/test_framework/coverage.py

    """
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="coverage")
        self.flag = '--coveragedir=%s' % self.dir

    def report_rpc_coverage(self):
        """
        Print out RPC commands that were unexercised by tests.

        """
        uncovered = self._get_uncovered_rpc_commands()

        if uncovered:
            print("Uncovered RPC commands:")
            print("".join(("  - %s\n" % command) for command in sorted(uncovered)))
            return False
        else:
            print("All RPC commands covered.")
            return True

    def cleanup(self):
        return shutil.rmtree(self.dir)

    def _get_uncovered_rpc_commands(self):
        """
        Return a set of currently untested RPC commands.

        """
        # This is shared from `test/functional/test-framework/coverage.py`
        reference_filename = 'rpc_interface.txt'
        coverage_file_prefix = 'coverage.'

        coverage_ref_filename = os.path.join(self.dir, reference_filename)
        coverage_filenames = set()
        all_cmds = set()
        covered_cmds = set()

        if not os.path.isfile(coverage_ref_filename):
            raise RuntimeError("No coverage reference found")

        with open(coverage_ref_filename, 'r', encoding="utf8") as coverage_ref_file:
            all_cmds.update([line.strip() for line in coverage_ref_file.readlines()])

        for root, _, files in os.walk(self.dir):
            for filename in files:
                if filename.startswith(coverage_file_prefix):
                    coverage_filenames.add(os.path.join(root, filename))

        for filename in coverage_filenames:
            with open(filename, 'r', encoding="utf8") as coverage_file:
                covered_cmds.update([line.strip() for line in coverage_file.readlines()])

        return all_cmds - covered_cmds


if __name__ == '__main__':
    main()
