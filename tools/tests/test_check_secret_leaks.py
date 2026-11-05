"""The leak checker finds secrets in files, staged changes and history, and fails when it cannot
look. Uses throwaway git repositories and made-up secrets only."""
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "check-secret-leaks.py"
spec = importlib.util.spec_from_file_location("check_secret_leaks", SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

SECRET = "Zq7-made-up-secret-value-1234"
OWNER_CODE = "H5RB2WTDK7M4Q9XP"


class LeakCheckerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git("init", "-q")
        (self.root / ".gitignore").write_text(".build/\n", encoding="utf-8")
        (self.root / "notes.txt").write_text("nothing secret here\n", encoding="utf-8")
        self.commit("first")
        (self.root / ".build").mkdir()
        self.write_setup({"password": SECRET, "setup_secret": OWNER_CODE})

    def git(self, *arguments):
        return subprocess.run(
            ["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", *arguments],
            cwd=self.root, check=True, capture_output=True, text=True).stdout

    def commit(self, message):
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", message)

    def write_setup(self, record, name="device-setup.json"):
        (self.root / ".build" / name).write_text(json.dumps(record), encoding="utf-8")

    def run_checker(self, *extra):
        import io
        import contextlib
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = checker.main(["--root", str(self.root), *extra])
        return code, out.getvalue(), err.getvalue()

    def test_clean_repository_passes(self):
        code, out, err = self.run_checker()
        self.assertEqual(code, 0, err)
        self.assertIn("PASS", out)

    def test_secret_in_a_working_file_is_found_and_never_printed(self):
        (self.root / "config.txt").write_text(f"token={SECRET}\n", encoding="utf-8")
        code, out, err = self.run_checker()
        self.assertEqual(code, 1)
        self.assertIn("config.txt", err)
        for text in (out, err):
            self.assertNotIn(SECRET, text)
            self.assertNotIn(OWNER_CODE, text)

    def test_owner_code_is_checked_too(self):
        (self.root / "readme.txt").write_text(f"code {OWNER_CODE}\n", encoding="utf-8")
        code, _, err = self.run_checker()
        self.assertEqual(code, 1)
        self.assertIn("readme.txt", err)

    def test_secret_only_in_the_staged_copy_is_found(self):
        path = self.root / "config.txt"
        path.write_text(f"token={SECRET}\n", encoding="utf-8")
        self.git("add", "config.txt")
        path.write_text("cleaned afterwards\n", encoding="utf-8")  # working file now fine
        code, _, err = self.run_checker()
        self.assertEqual(code, 1)
        self.assertIn("staged changes", err)

    def test_secret_in_an_old_commit_is_found_by_commit(self):
        path = self.root / "config.txt"
        path.write_text(f"token={SECRET}\n", encoding="utf-8")
        self.commit("oops")
        sha = self.git("rev-parse", "HEAD").strip()
        path.write_text("removed again\n", encoding="utf-8")
        self.commit("fix")
        code, _, err = self.run_checker()
        self.assertEqual(code, 1)
        self.assertIn(f"commit {sha[:12]}", err)
        # History scanning can be turned off, and a short window misses a deep commit.
        for _ in range(3):
            self.commit("later")
        self.assertEqual(self.run_checker("--history", "2")[0], 0)
        self.assertEqual(self.run_checker("--history", "0")[0], 0)
        self.assertEqual(self.run_checker("--history", "10")[0], 1)

    def test_an_unreadable_setup_file_fails_instead_of_skipping(self):
        (self.root / ".build" / "device-admin.json").write_text("{ not json", encoding="utf-8")
        code, _, err = self.run_checker()
        self.assertEqual(code, 1)
        self.assertIn("could NOT be completed", err)
        self.assertIn("device-admin.json", err)

    def test_git_that_will_not_answer_fails(self):
        (self.root / ".git" / "HEAD").write_text("garbage\n", encoding="utf-8")
        code, _, err = self.run_checker()
        self.assertEqual(code, 1)
        self.assertIn("could NOT be completed", err)

    def test_no_secrets_is_fine_unless_they_are_required(self):
        for item in (self.root / ".build").iterdir():
            item.unlink()
        code, out, _ = self.run_checker()
        self.assertEqual(code, 0)
        self.assertIn("nothing to look for", out)
        code, _, err = self.run_checker("--require-secrets")
        self.assertEqual(code, 1)
        self.assertIn("no local device secrets", err)

    def test_short_values_are_ignored_to_avoid_false_alarms(self):
        self.write_setup({"password": "short"})
        (self.root / ".build" / "device-setup.json").unlink()
        self.write_setup({"password": "short"}, "device-admin.json")
        (self.root / "notes.txt").write_text("a short word\n", encoding="utf-8")
        code, out, _ = self.run_checker()
        self.assertEqual(code, 0)
        self.assertIn("nothing to look for", out)

    def test_extra_log_files_are_scanned(self):
        log = self.root / "capture.log"
        log.write_text(f"serial output {SECRET}\n", encoding="utf-8")
        # an ignored file is not reviewable, but a named log is scanned anyway
        (self.root / ".gitignore").write_text(".build/\ncapture.log\n", encoding="utf-8")
        self.assertEqual(self.run_checker()[0], 0)
        code, _, err = self.run_checker("--log-file", str(log))
        self.assertEqual(code, 1)
        self.assertIn("capture.log", err)


if __name__ == "__main__":
    unittest.main()
