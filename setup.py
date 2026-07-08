from setuptools import find_packages, setup


setup(
    name="cfr-deep-stack",
    version="0.1.0",
    packages=find_packages(include=["cfr_deep_stack", "cfr_deep_stack.*"]),
    install_requires=["numpy>=1.21"],
    extras_require={"test": ["pytest>=7"], "deep": ["torch>=2"]},
)
