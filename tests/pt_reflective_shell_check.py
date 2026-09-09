"""Source integration guards for thin reflective shells; CPU physics is tested
by pt_material_layers_check.py using the actual GLSL Fresnel/transport code.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "code/renderer_vulkan"


class ReflectiveShell(unittest.TestCase):
    def test_picture_is_not_sampled_for_shell_reflection(self):
        source = (ROOT / "shaders/pt_integrator.glsl").read_text()
        uniform = source.split("if(context.uniformTint) {")[1].split("} else sampled=")[0]
        uniform = "\n".join(line.split("//", 1)[0] for line in uniform.splitlines())
        self.assertIn("textureLod", uniform)
        self.assertIn("vec2(0.5)", uniform)
        self.assertIn("textureQueryLevels", uniform)
        self.assertNotIn("observer", uniform)
        self.assertIn("if(!uniformTint && materials[id].layers[layerIndex].params.z==4)", source)
        appearance = (ROOT / "shaders/pt_reflective_shell.glsl").read_text()
        self.assertIn("layerSample(primitive,bary,layer,mat2(0),observer,true)", appearance)

    def test_shell_uses_actual_specular_ray_and_straight_transmission(self):
        source = (ROOT / "shaders/pt_integrator.glsl").read_text()
        optical = source.split("if(m.params.x==4) {")[1].split("if(m.params.x==2 || m.params.x==3)")[0]
        self.assertIn("reflectiveShellWeights(reflectance,reflectiveShellAppearance", optical)
        self.assertIn("pathS+=pathE", optical)
        self.assertIn("direction=reflect(direction,n)", optical)
        self.assertIn("paneReflection/max(paneProbability", optical)
        self.assertIn("paneTransmission/max(1-paneProbability", optical)
        self.assertIn("origin=shell ? world+direction*pc.parameters.x:", optical)
        self.assertIn("previousPDF=-1", optical)
        self.assertIn("if(bounce+1>=int(pc.sampling.y)) break;", optical)

    def test_shells_transmit_shadow_rays(self):
        source = (ROOT / "shaders/pt_integrator.glsl").read_text()
        shadow = source.split("vec3 visibility(")[1].split("vec3 basisSample(")[0]
        self.assertIn("materials[id].params.x==4 && materials[id].optical.y<0", shadow)
        self.assertIn("reflectiveShellWeights", shadow)
        self.assertIn("transmission*=t", shadow)

    def test_reconstruction_matches_thin_sheet_geometry(self):
        source = (ROOT / "shaders/pt_dielectric_geometry.glsl").read_text()
        self.assertIn("if(optics.y<0) return false;", source)
        self.assertIn("if(optics.y<0) {\n        outgoing=incident;\n        start=point+incident*bias;", source)
        native = (ROOT / "vk_pathtrace.c").read_text()
        shell = native.split("if (shell) {")[1].split("if (m->emission[2] != 0)")[0]
        self.assertIn("m->params[0] = 4", shell)
        self.assertIn("m->emission[1] = 0", shell)
        self.assertIn("m->params[2] = 0", shell)
        self.assertIn("m->optical[1] = -1", shell)


if __name__ == "__main__":
    unittest.main()
