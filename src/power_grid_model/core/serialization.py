# SPDX-FileCopyrightText: 2022 Contributors to the Power Grid Model project <dynamic.grid.calculation@alliander.com>
#
# SPDX-License-Identifier: MPL-2.0


"""
Power grid model (de)serialization
"""

from abc import ABC, abstractmethod
from ctypes import byref
from enum import IntEnum
from typing import Dict, Mapping, Tuple, Union

import numpy as np

from power_grid_model.core.error_handling import assert_no_error
from power_grid_model.core.index_integer import IdxC
from power_grid_model.core.power_grid_core import CharPtr, DeserializerPtr, SerializerPtr, WritableDatasetPtr
from power_grid_model.core.power_grid_core import power_grid_core as pgc
from power_grid_model.core.power_grid_dataset import CConstDataset, CWritableDataset
from power_grid_model.errors import PowerGridSerializationError


class SerializationType(IntEnum):
    """Serialization Format Types"""

    JSON = 0
    MSGPACK = 1


class Deserializer:
    """
    Deserializer for the Power grid model
    """

    _deserializer: DeserializerPtr
    _dataset_ptr: WritableDatasetPtr
    _dataset: CWritableDataset

    def __new__(cls, data: Union[str, bytes], serialization_type: SerializationType):
        instance = super().__new__(cls)

        raw_data = data if isinstance(data, bytes) else data.encode()
        instance._deserializer = pgc.create_deserializer_from_binary_buffer(
            raw_data, len(raw_data), serialization_type.value
        )
        assert_no_error()

        instance._dataset_ptr = pgc.deserializer_get_dataset(instance._deserializer)
        assert_no_error()

        instance._dataset = CWritableDataset(instance._dataset_ptr)
        assert_no_error()

        return instance

    def __del__(self):
        pgc.destroy_deserializer(self._deserializer)

    def load(self) -> Tuple[str, Mapping[str, Union[np.ndarray, Mapping[str, np.ndarray]]]]:
        """
        Load the deserialized data to a new dataset.

        Raises:
            ValueError if the data is inconsistent with the rest of the dataset or a component is unknown.
            PowerGridError if there was an internal error.

        Returns:
            A tuple containing:
                the type of the dataset
                the deserialized dataset in Power grid model input format
        """
        pgc.deserializer_parse_to_buffer(self._deserializer)
        return self._dataset.get_info().dataset_type(), self._dataset.get_data()


class Serializer(ABC):
    """
    Serializer for the Power grid model
    """

    _data: Dict[str, Union[np.ndarray, Mapping[str, np.ndarray]]]
    _dataset_type: str
    _dataset: CConstDataset
    _serializer: SerializerPtr

    def __new__(
        cls,
        dataset_type: str,
        data: Dict[str, Union[np.ndarray, Mapping[str, np.ndarray]]],
        serialization_type: SerializationType,
    ):
        instance = super().__new__(cls)

        instance._data = data
        instance._dataset_type = dataset_type

        if instance._data:
            first = next(iter(instance._data.values()))
            if not isinstance(first, np.ndarray):
                raise PowerGridSerializationError("Sparse batch datasets are not supported.")

        instance._dataset = CConstDataset(instance._dataset_type, instance._data)
        assert_no_error()

        instance._serializer = pgc.create_serializer(instance._dataset.get_dataset_ptr(), serialization_type.value)
        assert_no_error()

        return instance

    def __del__(self):
        pgc.destroy_serializer(self._serializer)

    def dump_str(self, *, use_compact_list: bool = False, indent: int = 2) -> str:
        """
        Dump the data to a decoded str; if supported.

        Args:
            use_compact_list: whether or not to use compact lists (sparse data). Defaults to False.
            indent:
                use specified indentation to make data more readable
                Use 0 or negative value for no indentation. Defaults to 2

        Returns:
            a serialized string containing the dataset
        """
        data = pgc.serializer_get_to_zero_terminated_string(self._serializer, int(use_compact_list), indent)
        assert_no_error()
        return data

    def dump_bytes(self, *, use_compact_list: bool = False) -> bytes:
        """
        Dump the data to a bytes object; if supported.

        Args:
            use_compact_list (bool, optional): whether or not to use compact lists (sparse data). Defaults to False.

        Returns:
            the raw bytes of the serialization of the datast
        """
        raw_data = CharPtr()  # pylint: disable(not-callable)
        size = IdxC()  # pylint: disable(not-callable)
        pgc.serializer_get_to_binary_buffer(self._serializer, int(use_compact_list), byref(raw_data), byref(size))
        assert_no_error()

        result = raw_data[: size.value]
        if not isinstance(result, bytes):
            raise PowerGridSerializationError("Invalid output data type")

        return result

    @abstractmethod
    def dump(self, *args, **kwargs):
        """
        Dump the data in the recommended data format (e.g. str or bytes, depending on the serialization_type).

        Args:
            *: any serialization_type dependent args
        """


class _BytesSerializer(Serializer):
    """
    Base type for serialization to bytes
    """

    dump = Serializer.dump_bytes


class _StringSerializer(Serializer):
    """
    Base type for serialization to str
    """

    dump = Serializer.dump_str


class JsonDeserializer(Deserializer):  # pylint: disable=too-few-public-methods
    """
    JSON deserializer for the Power grid model
    """

    def __new__(cls, data: Union[str, bytes]):
        return super().__new__(cls, data, SerializationType.JSON)


class MsgpackDeserializer(Deserializer):  # pylint: disable=too-few-public-methods
    """
    msgpack deserializer for the Power grid model
    """

    def __new__(cls, data: bytes):
        return super().__new__(cls, data, SerializationType.MSGPACK)


class JsonSerializer(_StringSerializer):  # pylint: disable=too-few-public-methods
    """
    JSON deserializer for the Power grid model
    """

    def __new__(cls, dataset_type: str, data: Dict[str, Union[np.ndarray, Mapping[str, np.ndarray]]]):
        return super().__new__(cls, dataset_type, data, SerializationType.JSON)


class MsgpackSerializer(_BytesSerializer):  # pylint: disable=too-few-public-methods
    """
    msgpack deserializer for the Power grid model
    """

    def __new__(cls, dataset_type: str, data: Dict[str, Union[np.ndarray, Mapping[str, np.ndarray]]]):
        return super().__new__(cls, dataset_type, data, SerializationType.MSGPACK)


def json_deserialize(data: Union[str, bytes]) -> Tuple[str, Mapping[str, Union[np.ndarray, Mapping[str, np.ndarray]]]]:
    """
    Load serialized JSON data to a new dataset.

    Args:
        data: the data to deserialize.

    Raises:
        ValueError if the data is inconsistent with the rest of the dataset or a component is unknown.
        PowerGridError if there was an internal error.

    Returns:
        A tuple containing:
            the type of the dataset.
            the deserialized dataset in Power grid model input format.
    """
    result = JsonDeserializer(data).load()
    assert_no_error()
    return result


def json_serialize(
    dataset_type: str,
    data: Dict[str, np.ndarray | Mapping[str, np.ndarray]],
    use_compact_list: bool = False,
    indent: int = 2,
) -> str:
    """
    Dump data to a JSON str.

    Args:
        dataset_type: the type of the dataset
        data: the dataset
        use_compact_list: whether or not to use compact lists (sparse data). Defaults to False.
        indent:
            use specified indentation to make data more readable
            Use 0 or negative value for no indentation. Defaults to 2

    Raises:
        PowerGridError if there was an internal error.

    Returns:
        a serialized string containing the dataset
    """
    result = JsonSerializer(dataset_type, data).dump(use_compact_list=use_compact_list, indent=indent)
    assert_no_error()
    return result


def msgpack_deserialize(data: bytes) -> Tuple[str, Mapping[str, Union[np.ndarray, Mapping[str, np.ndarray]]]]:
    """
    Load serialized msgpack data to a new dataset.

    Args:
        data: the data to deserialize.

    Raises:
        ValueError if the data is inconsistent with the rest of the dataset or a component is unknown.
        PowerGridError if there was an internal error.

    Returns:
        A tuple containing:
            the type of the dataset.
            the deserialized dataset in Power grid model input format.
    """
    result = MsgpackDeserializer(data).load()
    assert_no_error()
    return result


def msgpack_serialize(
    dataset_type: str, data: Dict[str, np.ndarray | Mapping[str, np.ndarray]], use_compact_list: bool = False
) -> bytes:
    """
    Dump the data to raw msgpack bytes.

    Args:
        dataset_type: the type of the dataset
        data: the dataset
        use_compact_list: whether or not to use compact lists (sparse data). Defaults to False.

    Raises:
        PowerGridError if there was an internal error.

    Returns:
        a serialized string containing the dataset
    """
    result = MsgpackSerializer(dataset_type, data).dump(use_compact_list=use_compact_list)
    assert_no_error()
    return result
