"""Validate the exported egg contract and keep private metadata out."""

import json
import pathlib
import re
import tempfile
import unittest

from render_pterodactyl_egg import render


class EggTests(unittest.TestCase):
    def test_import_contract_and_startup_variables(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "egg-kcdc.json"
            egg = render("ghcr.io/kingdomsconnected/kcdc-server:9.8.7-rc.1", path)
            self.assertEqual(json.loads(path.read_text()), egg)
            self.assertEqual(egg["meta"]["version"], "PTDL_v2")
            self.assertEqual(egg["author"], "contact@kingdomsconnected.com")
            self.assertEqual(list(egg["docker_images"].values()), ["ghcr.io/kingdomsconnected/kcdc-server:9.8.7-rc.1"])
            # One allocation: the game over UDP and HTTP over TCP on the same port
            # (Framework 40.2), so the startup names only Wings' built-in port.
            self.assertEqual(set(re.findall(r"\{\{([A-Z_]+)\}\}", egg["startup"])), {"SERVER_PORT"})
            self.assertNotIn("apiport", egg["startup"])
            self.assertEqual(egg["variables"], [])
            self.assertEqual(egg["config"]["stop"], "^C")
            self.assertEqual(json.loads(egg["config"]["startup"])["done"], "KCDC Server successfully started")
            self.assertEqual(json.loads(egg["config"]["files"]), {})
            self.assertEqual(json.loads(egg["config"]["logs"]), {})
            self.assertIsNone(egg["meta"]["update_url"])
            self.assertNotIn("github.com/kingdomsconnected/mod", path.read_text())
            self.assertNotIn("git clone", egg["scripts"]["installation"]["script"])

    def test_invalid_image_references_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for image in ["", "https://ghcr.io/owner/image", "owner/image", "owner/image:tag;id", "owner/image:$(id)", "owner/image:tag\nsecret"]:
                with self.subTest(image=image), self.assertRaises(ValueError):
                    render(image, pathlib.Path(directory) / "egg.json")


if __name__ == "__main__":
    unittest.main()
