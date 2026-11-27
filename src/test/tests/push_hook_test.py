"""推送门禁的行为测试：scripts/push 与 scripts/pre-push。

门禁（build → unit test → 全量 QA）由 scripts/push 在 git push 之前跑，全部通过后把
验证过的提交号写进标记文件 ``<git-dir>/fly-push-marker``；pre-push 钩子只核对标记文件
里的提交号与本次推送的提交是否一致，一致放行，否则报错退出并说明正确的推送方式。

这里在临时 git 仓库里做真实的 git push（本地裸仓库当远端，不联网），核对钩子的放行与
拦截规则。scripts/push 只测门禁之前的前置检查；门禁本身要构建和跑全量 QA，不在这里跑，
由真实仓库上的端到端运行验证。
"""
import os
import shutil
import subprocess
import tempfile
import unittest

_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, '..', '..', '..'))
PRE_PUSH = os.path.join(_project_root, 'scripts', 'pre-push')
PUSH = os.path.join(_project_root, 'scripts', 'push')

MARKER_NAME = 'fly-push-marker'


def _git_env():
    """隔离本机的 git 全局配置（签名、凭据、钩子路径等），测试只依赖仓库内的配置。"""
    env = dict(os.environ)
    env['GIT_CONFIG_GLOBAL'] = os.devnull
    env['GIT_CONFIG_NOSYSTEM'] = '1'
    env['GIT_TERMINAL_PROMPT'] = '0'
    return env


class PushHookTest(unittest.TestCase):
    """pre-push 钩子对标记文件的核对规则。"""

    def setUp(self):
        # 目录名带 fly_ 前缀：fly.sh test 结束时会兜底清扫 /tmp/fly_*（测试自己也会删）
        self.root = tempfile.mkdtemp(prefix='fly_push_hook_')
        self.origin = os.path.join(self.root, 'origin.git')
        self.work = os.path.join(self.root, 'work')
        self._git('-c', 'init.defaultBranch=main', 'init', '--bare', self.origin, cwd=self.root)
        self._git('-c', 'init.defaultBranch=main', 'clone', self.origin, self.work, cwd=self.root)
        self.git_dir = self._out('rev-parse', '--absolute-git-dir')
        self._git('config', 'user.name', 'push-hook-test')
        self._git('config', 'user.email', 'push-hook-test@example.invalid')
        # 钩子用一层转调脚本（不依赖源文件的执行位）：pre-push 的行为以 scripts/pre-push 为准
        hooks = os.path.join(self.git_dir, 'hooks')
        os.makedirs(hooks, exist_ok=True)
        with open(os.path.join(hooks, 'pre-push'), 'w') as f:
            f.write('#!/usr/bin/env bash\nexec bash "%s" "$@"\n' % PRE_PUSH)
        os.chmod(os.path.join(hooks, 'pre-push'), 0o755)
        self.marker = os.path.join(self.git_dir, MARKER_NAME)
        self._commit('first')

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    # ── 辅助 ──────────────────────────────────────────────────────────

    def _git(self, *args, cwd=None, check=True):
        return subprocess.run(['git'] + list(args), cwd=cwd or self.work, env=_git_env(),
                              capture_output=True, text=True, check=check)

    def _out(self, *args, cwd=None):
        return self._git(*args, cwd=cwd).stdout.strip()

    def _commit(self, text, branch=None):
        if branch is not None:
            self._git('checkout', '-b', branch)
        with open(os.path.join(self.work, 'file.txt'), 'a') as f:
            f.write(text + '\n')
        self._git('add', 'file.txt')
        self._git('commit', '-m', text)
        if branch is not None:
            self._git('checkout', 'main')

    def _head(self, ref='HEAD'):
        return self._out('rev-parse', ref)

    def _remote_head(self, ref):
        """远端引用指向的提交号；远端没有这个引用时返回 None。"""
        proc = subprocess.run(['git', 'rev-parse', '--verify', '--quiet', ref],
                              cwd=self.origin, env=_git_env(), capture_output=True, text=True)
        return proc.stdout.strip() if proc.returncode == 0 else None

    def _push(self, *args):
        proc = self._git('push', *args, check=False)
        return proc.returncode, proc.stdout + proc.stderr

    def _write_marker(self, content):
        with open(self.marker, 'w') as f:
            f.write(content)

    def _remove_marker(self):
        if os.path.exists(self.marker):
            os.remove(self.marker)

    def _assert_blocked(self, result):
        returncode, output = result
        self.assertNotEqual(returncode, 0, output)
        self.assertIn('PUSH BLOCKED', output)
        # 报错必须说明正确的推送方式
        self.assertIn('./scripts/push', output)
        self.assertIn('--no-verify', output)
        return output

    def _assert_allowed(self, result):
        returncode, output = result
        self.assertEqual(returncode, 0, output)
        self.assertIn('放行', output)
        return output

    # ── 钩子：拦截 ────────────────────────────────────────────────────

    def test_push_without_marker_is_blocked_and_explains_how(self):
        output = self._assert_blocked(self._push('origin', 'main'))
        self.assertIn('不存在', output)
        # 报错里列出本次要推送的引用
        self.assertIn('refs/heads/main', output)
        self.assertIsNone(self._remote_head('refs/heads/main'))

    def test_marker_of_another_commit_is_blocked(self):
        self._commit('second')
        other = self._head('HEAD~1')
        self._write_marker(other + '\n')
        output = self._assert_blocked(self._push('origin', 'main'))
        self.assertIn('不一致', output)
        self.assertIn(other, output)
        self.assertIn(self._head(), output)
        self.assertIsNone(self._remote_head('refs/heads/main'))

    def test_marker_that_is_not_a_commit_id_is_blocked(self):
        for content in ('', 'not-a-commit', 'abc', self._head()[:-1]):
            with self.subTest(content=content):
                self._write_marker(content)
                output = self._assert_blocked(self._push('origin', 'main'))
                if content:
                    self.assertIn('不是提交号', output)
                self.assertIsNone(self._remote_head('refs/heads/main'))

    def test_tag_pointing_at_another_commit_is_blocked(self):
        self._commit('second')
        self._git('tag', '-a', 'v-old', '-m', 'old', 'HEAD~1')
        self._write_marker(self._head() + '\n')
        self._assert_blocked(self._push('origin', 'v-old'))
        self.assertIsNone(self._remote_head('refs/tags/v-old'))

    def test_one_mismatching_ref_blocks_the_whole_push(self):
        self._commit('on side', branch='side')
        self._write_marker(self._head() + '\n')
        output = self._assert_blocked(self._push('origin', 'main', 'side'))
        self.assertIn('refs/heads/side', output)
        # 整次推送被拦下，连一致的那个引用也没有推上去
        self.assertIsNone(self._remote_head('refs/heads/main'))
        self.assertIsNone(self._remote_head('refs/heads/side'))

    def test_deleting_remote_ref_without_marker_is_blocked(self):
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'main:refs/heads/side'))
        self._remove_marker()
        self._assert_blocked(self._push('origin', '--delete', 'side'))
        self.assertEqual(self._remote_head('refs/heads/side'), self._head())

    # ── 钩子：放行 ────────────────────────────────────────────────────

    def test_marker_matching_the_pushed_commit_is_allowed(self):
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'main'))
        self.assertEqual(self._remote_head('refs/heads/main'), self._head())

    def test_marker_with_surrounding_whitespace_is_allowed(self):
        self._write_marker('  %s  \n\n' % self._head())
        self._assert_allowed(self._push('origin', 'main'))
        self.assertEqual(self._remote_head('refs/heads/main'), self._head())

    def test_annotated_tag_pointing_at_the_marker_commit_is_allowed(self):
        self._git('tag', '-a', 'v1', '-m', 'release', 'HEAD')
        self.assertNotEqual(self._head('v1'), self._head('HEAD'))    # 附注标签有自己的对象号
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'v1'))
        self.assertEqual(self._remote_head('refs/tags/v1'), self._head('v1'))

    def test_lightweight_tag_pointing_at_the_marker_commit_is_allowed(self):
        self._git('tag', 'light', 'HEAD')
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'light'))
        self.assertEqual(self._remote_head('refs/tags/light'), self._head())

    def test_same_commit_pushed_to_other_remote_branch_is_allowed(self):
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'main:refs/heads/backup'))
        self.assertEqual(self._remote_head('refs/heads/backup'), self._head())

    def test_deleting_remote_ref_with_marker_is_allowed(self):
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'main:refs/heads/side'))
        self._assert_allowed(self._push('origin', '--delete', 'side'))
        self.assertIsNone(self._remote_head('refs/heads/side'))

    def test_marker_survives_the_push(self):
        """标记不为一次性的：同一个提交重推（例如网络失败后）不必再跑门禁。"""
        self._write_marker(self._head() + '\n')
        self._assert_allowed(self._push('origin', 'main'))
        self.assertTrue(os.path.isfile(self.marker))


class PushScriptPrecheckTest(unittest.TestCase):
    """scripts/push 在门禁之前的前置检查。门禁本身不在这里跑。"""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='fly_push_script_')
        self.work = os.path.join(self.root, 'work')
        os.makedirs(self.work)
        self._git('-c', 'init.defaultBranch=main', 'init', '.')
        self._git('config', 'user.name', 'push-script-test')
        self._git('config', 'user.email', 'push-script-test@example.invalid')
        with open(os.path.join(self.work, 'file.txt'), 'w') as f:
            f.write('first\n')
        self._git('add', 'file.txt')
        self._git('commit', '-m', 'first')
        self.git_dir = self._out('rev-parse', '--absolute-git-dir')
        self.marker = os.path.join(self.git_dir, MARKER_NAME)

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def _git(self, *args, cwd=None):
        return subprocess.run(['git'] + list(args), cwd=cwd or self.work, env=_git_env(),
                              capture_output=True, text=True, check=True)

    def _out(self, *args):
        return self._git(*args).stdout.strip()

    def _run_push_script(self, *args, cwd=None):
        proc = subprocess.run(['bash', PUSH] + list(args), cwd=cwd or self.work,
                              env=_git_env(), capture_output=True, text=True)
        return proc.returncode, proc.stdout + proc.stderr

    def _touch(self, name, text='x\n'):
        with open(os.path.join(self.work, name), 'w') as f:
            f.write(text)

    def test_help_prints_usage_and_exits_zero(self):
        for flag in ('-h', '--help'):
            with self.subTest(flag=flag):
                returncode, output = self._run_push_script(flag)
                self.assertEqual(returncode, 0, output)
                self.assertIn('scripts/push', output)
                self.assertIn('git push', output)

    def test_clean_tree_passes_the_precheck_and_reaches_the_gate(self):
        """工作区干净时前置检查通过，脚本进入门禁的第一阶段（构建）。

        临时仓库里没有 fly.sh，构建阶段立刻失败——这正好证明前置检查没有拦、
        也没有在门禁之前写标记。
        """
        returncode, output = self._run_push_script()
        self.assertNotEqual(returncode, 0)
        self.assertIn('[1/3] BUILD', output)
        self.assertIn('PUSH BLOCKED', output)
        self.assertFalse(os.path.exists(self.marker))

    def test_modified_tracked_file_is_refused_before_the_gate(self):
        self._touch('file.txt', 'changed\n')
        returncode, output = self._run_push_script()
        self.assertNotEqual(returncode, 0)
        self.assertIn('PUSH BLOCKED', output)
        self.assertIn('M file.txt', output)
        self.assertNotIn('[1/3] BUILD', output)
        self.assertFalse(os.path.exists(self.marker))

    def test_staged_change_is_refused_before_the_gate(self):
        self._touch('new.txt')
        self._git('add', 'new.txt')
        returncode, output = self._run_push_script()
        self.assertNotEqual(returncode, 0)
        self.assertIn('A  new.txt', output)
        self.assertNotIn('[1/3] BUILD', output)

    def test_untracked_file_is_refused_before_the_gate(self):
        self._touch('stray.txt')
        returncode, output = self._run_push_script()
        self.assertNotEqual(returncode, 0)
        self.assertIn('?? stray.txt', output)
        self.assertNotIn('[1/3] BUILD', output)
        self.assertFalse(os.path.exists(self.marker))

    def test_outside_a_git_repository_is_refused(self):
        returncode, output = self._run_push_script(cwd=self.root)
        self.assertNotEqual(returncode, 0)
        self.assertIn('git', output)
        self.assertNotIn('[1/3] BUILD', output)


if __name__ == '__main__':
    unittest.main()
