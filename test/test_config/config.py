import locale
import os
import platform
import pkgutil
import re
import shlex
import shutil
import sys

import lit.Test  # pylint: disable=import-error,no-name-in-module
import lit.util  # pylint: disable=import-error,no-name-in-module
import lit.formats

def loadSiteConfig(lit_config, config, param_name, env_name):
    # We haven't loaded the site specific configuration (the user is
    # probably trying to run on a test file directly, and either the site
    # configuration hasn't been created by the build system, or we are in an
    # out-of-tree build situation).
    site_cfg = lit_config.params.get(param_name,
                                     os.environ.get(env_name))
    if not site_cfg:
        lit_config.warning('No site specific configuration file found!'
                           ' Running the tests in the default configuration.')
    elif not os.path.isfile(site_cfg):
        lit_config.fatal(
            "Specified site configuration file does not exist: '%s'" %
            site_cfg)
    else:
        lit_config.note('using site specific configuration at %s' % site_cfg)
        ld_fn = lit_config.load_config

        # Null out the load_config function so that lit.site.cfg doesn't
        # recursively load a config even if it tries.
        # TODO: This is one hell of a hack. Fix it.
        def prevent_reload_fn(*args, **kwargs):
            pass
        lit_config.load_config = prevent_reload_fn
        ld_fn(config, site_cfg)
        lit_config.load_config = ld_fn


class Configuration(object):
    # pylint: disable=redefined-outer-name
    def __init__(self, lit_config, config):
        self.lit_config = lit_config
        self.config = config
        self.cxx = self.get_lit_conf('cxx')
        if self.cxx is None:
            lit_config.fatal('failed to find compiler')
        self.source_root = self.get_lit_conf('source_root')
        self.build_root = self.get_lit_conf('build_root')
        assert self.source_root is not None
        assert self.build_root is not None
        self.plugin_name = 'libtemplate_parser.so'
        if platform.system() == 'Darwin':
            self.plugin_name = 'libtemplate_parser.dylib'
        self.plugin = os.path.join(self.build_root, 'lib', self.plugin_name)

    def get_lit_conf(self, name, default=None):
        val = self.lit_config.params.get(name, None)
        if val is None:
            val = getattr(self.config, name, None)
            if val is None:
                val = default
        return val

    def get_lit_bool(self, name, default=None):
        conf = self.get_lit_conf(name)
        if conf is None:
            return default
        if isinstance(conf, bool):
            return conf
        if not isinstance(conf, str):
            raise TypeError('expected bool or string')
        if conf.lower() in ('1', 'true'):
            return True
        if conf.lower() in ('', '0', 'false'):
            return False
        self.lit_config.fatal(
            "parameter '{}' should be true or false".format(name))

    def configure(self):
        sub = self.config.substitutions
        sub.append(('%cxx', ' '.join([self.cxx, '-stdlib=libc++'])))
        plugin_flags = ['-Xclang', '-load', '-Xclang', self.plugin, '-Xclang',
                        '-plugin', '-Xclang', 'template-tools']
        plugin_flags += ['-fsyntax-only']
        sub.append(('%plugin_flags', ' '.join(plugin_flags)))
        plugin_arg_str = '-Xclang -plugin-arg-template-tools -Xclang '
        sub.append(('%plugin_arg=', plugin_arg_str))

    def get_test_format(self):
        return lit.formats.ShTest(True)
